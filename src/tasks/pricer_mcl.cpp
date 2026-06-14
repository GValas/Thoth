#include "thoth.hpp"
#include "pricer_mcl.hpp"
#include "cancellation.hpp"
#include "mono.hpp"
#include "progress_bar.hpp"
#include "path_generator.hpp"
#include <algorithm>
#include <gsl/gsl_multifit.h>

PricerMCL::PricerMCL( const string& ObjectName,
                      YamlConfig& YamlConfig ) : Pricer( ObjectName, YamlConfig )
{
    gsl_rng_env_setup(); //!< must precede gsl_rng_alloc for GSL_RNG_SEED/TYPE to apply
    _gsl_r = gsl_rng_alloc( gsl_rng_default );
}

PricerMCL::~PricerMCL() = default;

//! diffusion dates
void PricerMCL::InitDates()
{

    //! diffusion dates = fixing_dates + mcl dates + without today
    set<date> fixing_dates = _book->GetFixingDates();
    date last_date = *( --( fixing_dates.end() ) );
    days days_step( _configuration->_mcl->_max_time_step );
    for ( date d = _today;
          d < last_date;
          d += days_step )
    {
        _diffusion_dates.insert( d );
    }
    _diffusion_dates.insert( fixing_dates.begin(), fixing_dates.end() );
    //_diffusion_dates.erase( _today );
}

//! check the mcl configuration and correlation are present
void PricerMCL::PreCheck_()
{
    //! mcl pricing needs its parameter object (config field "mcl")
    if ( !_configuration->_mcl )
    {
        ERR( "book pricing '" + _name + "' uses the mcl method but its configuration has no 'mcl' object" );
    }

    //! MCL diffusion correlates the underlyings' Brownian motions, so a
    //! correlation matrix is mandatory (guards the unconditional derefs in
    //! ComputeCholeskyMatrix / CorrelateBrownianNodes below).
    if ( !_correlation )
    {
        ERR( "book pricing '" + _name + "' requires a correlation matrix" );
    }
}

//! price the whole book by Monte-Carlo. Re-runnable: the node tree is rebuilt
//! from the current market each call and the RNG is reseeded, so the Greeks
//! bumps share common random numbers with the base scenario (stable bumps).
void PricerMCL::PriceBook_()
{
    Pricer::InitPricing();

    //! reset to a clean tree and a fixed RNG seed for reproducible, common-random
    //! pricing across the base and bumped scenarios
    _diffusion_dates.clear();
    _collector.Reset();
    _scenario_roots.clear();
    _scenario_premium.clear();
    gsl_rng_set( _gsl_r, (unsigned long)_configuration->_mcl->_seed );

    //! single-tree Greeks: build the bump sub-trees alongside the base tree (and
    //! price them in the same path sweep) when delta/gamma/vega/rho are requested
    //! and the book supports it. theta is handled separately (reprice), and the
    //! theta reprice / unsupported books set _suppress_scenarios.
    const bool spot_greeks = _request_delta || _request_gamma || _request_vega || _request_rho;
    _build_greek_scenarios = spot_greeks && !_suppress_scenarios && CanSingleTreeGreeks_();

    Tree_Init_();
    Tree_Run_();
    Tree_Read_();
    PriceAmerican(); //!< override American contracts via Longstaff-Schwartz
}

//! single-tree Greeks need to isolate one contract's spot bump within a shared
//! path; that requires a plain (Mono) underlying and no American early exercise
//! (the LSM regression is a separate post-pass). Otherwise fall back to bumps.
bool PricerMCL::CanSingleTreeGreeks_() const
{
    for ( Contract* c : _book->GetOptionList() )
    {
        if ( c->PDE_IsAmerican() )
        {
            return false;
        }
        if ( !dynamic_cast<Mono*>( c->GetUnderlying() ) )
        {
            return false;
        }
    }
    return true;
}

//! build the delta/gamma/vega/rho bump sub-trees. Each bump mutates the market,
//! builds a scenario-tagged copy of the book sub-tree (which captures the bumped
//! spot/vol/rate while reusing the shared Brownian/noise nodes), then restores
//! the market. The roots are priced together with the base tree in one sweep.
void PricerMCL::BuildGreekScenarios_()
{
    //! snapshot the singles first: building a sub-tree must not invalidate the
    //! iterator, and each spot bump is relative to the unbumped spot
    const vector<Single*> singles( _single_set.begin(), _single_set.end() );

    auto build = [&]( const string& Tag, const std::function<void()>& Mutate,
                      const std::function<void()>& Restore )
    {
        Mutate();
        _collector.SetScenarioSuffix( Tag );
        MonteCarloNode* root = _book->GetNode( _collector );
        _collector.SetScenarioSuffix( "" );
        Restore();
        _scenario_roots.emplace_back( Tag, root );
    };

    //! delta / gamma : per-underlying relative spot bump (small for delta, wider
    //! for gamma), matching the bump-and-revalue conventions in Pricer
    for ( Single* s : singles )
    {
        const double spot = s->GetSpot();
        if ( _request_delta )
        {
            const double h = GREEK_SPOT_BUMP * spot;
            build( "@D+:" + s->GetName(), [&] { s->SetSpot( spot + h ); }, [&] { s->SetSpot( spot ); } );
            build( "@D-:" + s->GetName(), [&] { s->SetSpot( spot - h ); }, [&] { s->SetSpot( spot ); } );
        }
        if ( _request_gamma )
        {
            const double h = GREEK_GAMMA_BUMP * spot;
            build( "@G+:" + s->GetName(), [&] { s->SetSpot( spot + h ); }, [&] { s->SetSpot( spot ); } );
            build( "@G-:" + s->GetName(), [&] { s->SetSpot( spot - h ); }, [&] { s->SetSpot( spot ); } );
        }
    }

    //! vega : parallel vol bump on every underlying
    if ( _request_vega )
    {
        build( "@V+", [&] { ApplyVolShift_( GREEK_VOL_BUMP ); }, [&] { ApplyVolShift_( 0 ); } );
        build( "@V-", [&] { ApplyVolShift_( -GREEK_VOL_BUMP ); }, [&] { ApplyVolShift_( 0 ); } );
    }

    //! rho : parallel rate bump on every currency's curve
    if ( _request_rho )
    {
        build( "@R+", [&] { ApplyRateShift_( GREEK_RATE_BUMP ); }, [&] { ApplyRateShift_( 0 ); } );
        build( "@R-", [&] { ApplyRateShift_( -GREEK_RATE_BUMP ); }, [&] { ApplyRateShift_( 0 ); } );
    }
}
//! single-tree Greeks: delta/gamma/vega/rho come from the bump sub-trees priced
//! in the base path sweep (read back as _scenario_premium); theta is a separate
//! reprice (rolling today changes the diffusion-date grid). Unsupported books
//! (American / non-Mono) fall back to the bump-and-revalue base implementation.
void PricerMCL::ComputeGreeks_()
{
    if ( !_build_greek_scenarios )
    {
        Pricer::ComputeGreeks_(); //!< American / non-Mono : bump-and-revalue
        return;
    }

    const double p0 = _premium;

    //! delta / gamma : summed over the per-underlying spot bumps
    if ( _request_delta || _request_gamma )
    {
        double delta = 0;
        double gamma = 0;
        for ( Single* s : _single_set )
        {
            const double spot = s->GetSpot();
            if ( _request_delta )
            {
                const double h = GREEK_SPOT_BUMP * spot;
                delta += ( _scenario_premium.at( "@D+:" + s->GetName() ) -
                           _scenario_premium.at( "@D-:" + s->GetName() ) ) /
                         ( 2 * h );
            }
            if ( _request_gamma )
            {
                const double h = GREEK_GAMMA_BUMP * spot;
                gamma += ( _scenario_premium.at( "@G+:" + s->GetName() ) - 2 * p0 +
                           _scenario_premium.at( "@G-:" + s->GetName() ) ) /
                         ( h * h );
            }
        }
        _delta = delta;
        _gamma = gamma;
    }

    //! vega / rho : per 1 vol point / per 1% rate move (matches Pricer conventions)
    if ( _request_vega )
    {
        _vega = ( _scenario_premium.at( "@V+" ) - _scenario_premium.at( "@V-" ) ) /
                ( 2 * GREEK_VOL_BUMP ) * 0.01;
    }
    if ( _request_rho )
    {
        _rho = ( _scenario_premium.at( "@R+" ) - _scenario_premium.at( "@R-" ) ) /
               ( 2 * GREEK_RATE_BUMP ) * 0.01;
    }

    //! theta : roll today one calendar day forward and reprice (base-only tree;
    //! the diffusion-date grid changes, so it cannot share the single tree). Only
    //! ONE extra graph is generated: theta reuses the already-computed base
    //! premium p0, and the base book/contract premiums are snapshotted and
    //! restored without a second reprice.
    if ( _request_theta )
    {
        const date base_today = _today;

        //! snapshot the base outputs (the roll reprice overwrites them)
        const double book_premium = _book->GetPremium();
        const double book_trust = _book->GetPremiumTrust();
        vector<double> contract_premium;
        vector<double> contract_trust;
        for ( Contract* c : _book->GetOptionList() )
        {
            contract_premium.push_back( c->GetPremium() );
            contract_trust.push_back( c->GetPremiumTrust() );
        }

        _quiet_pricing = true;
        _suppress_scenarios = true;
        _today = base_today + days( 1 );
        PriceBook_(); //!< the single extra graph
        const double p1 = _book->GetPremium();
        _today = base_today;
        _suppress_scenarios = false;
        _quiet_pricing = false;

        //! restore the base scenario (dates + premiums) without repricing
        _book->SetToday( base_today );
        _currency->SetToday( base_today );
        _book->SetPremium( book_premium );
        _book->SetPremiumTrust( book_trust );
        size_t i = 0;
        for ( Contract* c : _book->GetOptionList() )
        {
            c->SetPremium( contract_premium[i] );
            c->SetPremiumTrust( contract_trust[i] );
            i++;
        }

        _theta = p1 - p0;
    }

    _premium = _book->GetPremium();
}

void PricerMCL::CorrelateBrownianNodes_()
{
    //! more than 1 asset : correlation of noises
    if ( _single_set.size() > 1 )
    {

        //! compute cholesky matrix
        ComputeCholeskyMatrix();

        //! correlated noises = sum product
        SingleSet::iterator u, v;
        for ( u = _single_set.begin();
              u != _single_set.end();
              u++ )
        {

            //! brownian exist at this date, for this underlying
            if ( BrownianNode* B = _collector.GetBrownianNode( ( *u )->GetName() + "#brownian" ) )
            {

                //! noise = sum product correl/white_noise
                string node_name = ( *u )->GetName() + "#noise";
                CorrelatedNoiseNode* correlated_noise_node = _collector.NewNode<CorrelatedNoiseNode>( node_name );

                //! link to other white_noises
                for ( v = _single_set.begin();
                      v != _single_set.end();
                      v++ )
                {

                    double x = _correlation->GetCholeskyValue( ( *u )->GetName(), ( *v )->GetName() );
                    if ( x != 0 )
                    {
                        MonteCarloNode* n = _collector.GetNode( ( *v )->GetName() + "#white_noise" );
                        MonteCarloNode* c = _correlation->GetCholeskyNode( _collector,
                                                                           ( *u )->GetName(),
                                                                           ( *v )->GetName() ); // constant correlation
                        correlated_noise_node->PushNoiseNode( n );
                        correlated_noise_node->PushCholeskyNode( c );
                    }
                }

                //! link to brownian brownian link
                B->SetNoiseNode( correlated_noise_node );
            }
        }
    }
}

void PricerMCL::CreateContractualNodes_()
{
    _root = _book->GetNode( _collector );
}

void PricerMCL::CreateBrownianNodes_()
{

    SingleSet::iterator u;
    for ( u = _single_set.begin();
          u != _single_set.end();
          u++ )
    {
        //! noise
        string node_name = ( *u )->GetName() + "#white_noise";
        NoiseNode* N = _collector.NewNode<NoiseNode>( node_name );
        N->SetRandomGenerator( _gsl_r );

        //! brownian
        node_name = ( *u )->GetName() + "#brownian";
        BrownianNode* B = _collector.NewBrownianNode( node_name );
        B->SetNoiseNode( N );

        //! stochastic vol (Heston) underlyings need a second, independent noise
        //! for the variance process (pseudo-random; Sobol stays on the spot noise)
        if ( ( *u )->GetVolatility()->IsStochastic() )
        {
            NoiseNode* VN = _collector.NewNode<NoiseNode>( ( *u )->GetName() + "#vol_white_noise" );
            VN->SetRandomGenerator( _gsl_r );
        }
    }
}

//! wire the Sobol + Brownian-bridge generator into the per-underlying noise
//! nodes (only when use_sobol is set); otherwise the nodes keep drawing
//! independent pseudo-random gaussians.
void PricerMCL::SetupQuasiRandom_()
{
    if ( !_configuration->_mcl->_use_sobol )
    {
        return;
    }

    //! diffusion year-fractions (the set is sorted; first date is today -> 0)
    vector<double> times;
    times.reserve( _diffusion_dates.size() );
    for ( const date& d : _diffusion_dates )
    {
        times.push_back( YearFraction( _today, d ) );
    }

    //! one factor per diffused underlying (name-ordered, for reproducibility).
    //! a non-zero seed (cluster slave) offsets the Sobol sequence by seed*paths
    //! points so each slave draws a disjoint block.
    int factors = (int)_single_set.size();
    uint64_t sobol_skip = (uint64_t)_configuration->_mcl->_seed * (uint64_t)_configuration->_mcl->_paths;
    _path_generator = std::make_unique<PathGenerator>( times, factors, true, _gsl_r, sobol_skip );

    int f = 0;
    for ( Single* s : _single_set )
    {
        if ( NoiseNode* nn = dynamic_cast<NoiseNode*>( _collector.GetNode( s->GetName() + "#white_noise" ) ) )
        {
            nn->SetNoiseBuffer( _path_generator->Buffer( f ) );
        }
        f++;
    }
}

void PricerMCL::ComputeCholeskyMatrix()
{
    vector<string> single_name_list;
    vector<string> fx_name_list;
    SingleSet::iterator s;
    for ( s = _single_set.begin();
          s != _single_set.end();
          s++ )
    {
        if ( ( *s )->GetKind() == KIND_FOREX )
        {
            fx_name_list.push_back( ( *s )->GetName() );
        }
        else
        {
            single_name_list.push_back( ( *s )->GetName() );
        }
    }
    single_name_list.insert( single_name_list.end(), fx_name_list.begin(), fx_name_list.end() );
    _correlation->ComputeCholeskyMatrix( single_name_list );
}

//!
void PricerMCL::Tree_Init_()
{
    //! node collector
    InitDates();
    _collector.SetDiffusionDates( _diffusion_dates );

    //! node tree
    CreateBrownianNodes_();
    SetupQuasiRandom_(); //!< wire Sobol + bridge increments into the noise nodes
    CorrelateBrownianNodes_();
    CreateContractualNodes_();

    //! base root + (optionally) the Greek bump sub-trees, all sorted into one
    //! schedule so a single path sweep prices price and Greeks together
    vector<MonteCarloNode*> roots{ _root };
    if ( _build_greek_scenarios )
    {
        BuildGreekScenarios_();
        for ( auto& tagged : _scenario_roots )
        {
            roots.push_back( tagged.second );
        }
    }

    //! sort nodes
    _collector.SortNodes( roots );

    //! debug : dump the built node graph (once, on the visible base build)
    if ( _debug && _debug->_generate_nodes_graph && !_quiet_pricing )
    {
        const string path = _configuration->_log_path + _name + "_nodes.dot";
        _collector.ExportGraph( path );
        LOG( "MCL", "node graph written to " + path );
    }

    //! American contracts : record their underlying spot paths (opt-in)
    SetupAmericanRecording();

    //! status
    {
        std::ostringstream oss;
        oss << _collector.GetNodeNumber() << " created nodes";
        LOG( "MCL", oss.str() );
    }
    {
        std::ostringstream oss;
        oss << "diffusion dates = " << _diffusion_dates.size() << ", "
            << "contracts = " << _book->GetOptionList().size() << ", "
            << "underlings = " << _single_set.size() << ", "
            << "currencies = " << _currency_set.size();
        LOG( "MCL", oss.str() );
    }
    {
        std::ostringstream oss;
        oss << "drawings = " << _configuration->_mcl->_paths << ", "
            << "max time step = " << _configuration->_mcl->_max_time_step << ", "
            << "vol time step = " << _configuration->_mcl->_vol_time_step;
        LOG( "MCL", oss.str() );
    }
}

void PricerMCL::Tree_Run_()
{
    //! report resident memory before the (potentially large) path loop starts
    //! (skipped for the silent inner re-prices of bump-and-revalue Greeks)
    if ( !_quiet_pricing )
    {
        string mem = CurrentMemoryUsage();
        LOG( "MCL", "starting pricing" + ( mem.empty() ? string() : ", memory = " + mem ) );
    }

    //! iterations
    int n = _configuration->_mcl->_paths;
    ProgressBar bar( "MCL", n, !_quiet_pricing );
    for ( int i = 0; i < n; i++ )
    {
        //! bail out promptly if the request was cancelled (client disconnected)
        cancellation::CancellationPoint();

        //! refresh the Sobol + bridge increments for this path (no-op if unset)
        if ( _path_generator )
        {
            _path_generator->NextPath();
        }

        // 1 cycle
        _collector.PriceNodes();

        //! snapshot recorded spot paths (no-op unless American contracts)
        _collector.RecordPath();

        //! live progress (price/trust computed only when the bar redraws)
        bar.Update( i + 1, [&]()
                    { return "price = " + ToString( _root->GetIndicatorValue( 0 ) ) +
                             ", trust = " + ToString( _root->GetIndicatorTrust( 0 ) ); } );
    }
    bar.Done( "price = " + ToString( _root->GetIndicatorValue( 0 ) ) +
              ", trust = " + ToString( _root->GetIndicatorTrust( 0 ) ) );

    //! report the recorded American underlying paths (martingale sanity)
    LogRecordings();
}

//! register the underlying spot of every American contract for path recording
void PricerMCL::SetupAmericanRecording()
{
    size_t n = (size_t)_configuration->_mcl->_paths;
    for ( Contract* c : _book->GetOptionList() )
    {
        if ( !c->PDE_IsAmerican() )
        {
            continue;
        }
        string udl = c->GetUnderlying()->GetName();
        MonteCarloNode* spot = _collector.GetNode( udl + "#spot" );
        if ( !spot )
        {
            continue;
        }
        vector<size_t> grid = _collector.DiffusionIndicesUpTo( c->GetMaturityDate() );
        _collector.StartRecording( spot, grid, n );
        std::ostringstream oss;
        oss << "recording " << grid.size() << " exercise dates on '"
            << udl << "' for American contract '" << c->GetName() << "'";
        LOG( "MCL", oss.str() );
    }
}

//! martingale check on each recorded underlying : E[ S_T ] ~ S_0 * exp( r T )
void PricerMCL::LogRecordings()
{
    if ( !_collector.IsRecording() )
    {
        return;
    }
    for ( Contract* c : _book->GetOptionList() )
    {
        if ( !c->PDE_IsAmerican() )
        {
            continue;
        }
        string udl = c->GetUnderlying()->GetName();
        const gsl_matrix* paths = _collector.RecordedPaths( udl + "#spot" );
        if ( !paths || paths->size2 == 0 )
        {
            continue;
        }
        size_t last = paths->size2 - 1;
        double mean = 0;
        for ( size_t i = 0; i < paths->size1; i++ )
        {
            mean += gsl_matrix_get( paths, i, last );
        }
        mean /= paths->size1;
        std::ostringstream oss;
        oss << "recorded '" << udl << "' paths : E[S_T] = " << mean
            << " (" << paths->size1 << " x " << paths->size2 << ")";
        LOG( "MCL", oss.str() );
    }
}

//! price every American contract by Longstaff-Schwartz, then re-aggregate the book
void PricerMCL::PriceAmerican()
{
    if ( !_collector.IsRecording() )
    {
        return;
    }

    //! override each American contract's premium with its LSM value
    for ( Contract* c : _book->GetOptionList() )
    {
        if ( !c->PDE_IsAmerican() )
        {
            continue;
        }
        double trust = 0;
        double premium = PriceAmericanLSM( c, trust );
        c->SetPremium( premium );
        c->SetPremiumTrust( trust );
        std::ostringstream oss;
        oss << "american '" << c->GetName() << "' (LSM) premium = " << premium;
        LOG( "MCL", oss.str() );
    }

    //! re-aggregate the book premium from the (now possibly American) contracts
    double book_premium = 0;
    for ( Contract* c : _book->GetOptionList() )
    {
        double fx = 1;
        if ( _currency != c->GetPremiumCurrency() )
        {
            fx = _correlation->GetFxSpot( _currency->GetName(),
                                          c->GetPremiumCurrency()->GetName() );
        }
        book_premium += c->GetPremium() * fx;
    }
    _book->SetPremium( book_premium );
}

//! Longstaff-Schwartz on the recorded spot paths of the contract's underlying.
//! Backward induction over the exercise grid; at each date the continuation
//! value of the in-the-money paths is regressed on { 1, m, m^2 } (m = S/S0),
//! and a path exercises when its intrinsic value beats the estimated
//! continuation. Intrinsic uses the contract's own payoff (PDE_EvalFlow).
double PricerMCL::PriceAmericanLSM( Contract* Contract, double& Trust )
{
    Trust = 0;
    string udl = Contract->GetUnderlying()->GetName();
    const gsl_matrix* S = _collector.RecordedPaths( udl + "#spot" );
    vector<double> tau = _collector.RecordedTau( udl + "#spot" );
    if ( !S || S->size2 < 2 )
    {
        return 0;
    }

    size_t N = S->size1; //!< paths
    size_t M = S->size2; //!< columns: tau[0]=0 (today) .. tau[M-1]=maturity
    double r = Contract->GetPremiumCurrency()->GetRate()->GetCurveValue( Contract->GetMaturityDate() );
    double s0 = gsl_matrix_get( S, 0, 0 ); //!< initial spot (constant across paths)

    //! cashflow per path, expressed as value at the current backward time step
    vector<double> cf( N );
    for ( size_t p = 0; p < N; p++ )
    {
        cf[p] = Contract->PDE_EvalFlow( gsl_matrix_get( S, p, M - 1 ) ); //!< at maturity
    }

    //! backward induction over the interior exercise dates
    for ( int t = (int)M - 2; t >= 1; t-- )
    {
        double df = exp( -r * ( tau[t + 1] - tau[t] ) );
        for ( size_t p = 0; p < N; p++ )
        {
            cf[p] *= df; //!< discount future cashflow to this date
        }

        //! in-the-money paths
        vector<size_t> itm;
        for ( size_t p = 0; p < N; p++ )
        {
            if ( Contract->PDE_EvalFlow( gsl_matrix_get( S, p, t ) ) > 0 )
            {
                itm.push_back( p );
            }
        }
        if ( itm.size() < 3 )
        {
            continue; //!< not enough points to regress -> never exercise here
        }

        //! regress discounted continuation cashflow on { 1, m, m^2 }
        size_t ni = itm.size();
        gsl_matrix* X = gsl_matrix_alloc( ni, 3 );
        gsl_vector* y = gsl_vector_alloc( ni );
        for ( size_t k = 0; k < ni; k++ )
        {
            double m = gsl_matrix_get( S, itm[k], t ) / s0;
            gsl_matrix_set( X, k, 0, 1.0 );
            gsl_matrix_set( X, k, 1, m );
            gsl_matrix_set( X, k, 2, m * m );
            gsl_vector_set( y, k, cf[itm[k]] );
        }
        gsl_vector* beta = gsl_vector_alloc( 3 );
        gsl_matrix* cov = gsl_matrix_alloc( 3, 3 );
        double chisq = 0;
        gsl_multifit_linear_workspace* w = gsl_multifit_linear_alloc( ni, 3 );
        gsl_multifit_linear( X, y, beta, cov, &chisq, w );
        double b0 = gsl_vector_get( beta, 0 );
        double b1 = gsl_vector_get( beta, 1 );
        double b2 = gsl_vector_get( beta, 2 );

        //! exercise where intrinsic beats the estimated continuation
        for ( size_t p : itm )
        {
            double s = gsl_matrix_get( S, p, t );
            double m = s / s0;
            double continuation = b0 + b1 * m + b2 * m * m;
            double intrinsic = Contract->PDE_EvalFlow( s );
            if ( intrinsic >= continuation )
            {
                cf[p] = intrinsic;
            }
        }

        gsl_multifit_linear_free( w );
        gsl_matrix_free( X );
        gsl_vector_free( y );
        gsl_vector_free( beta );
        gsl_matrix_free( cov );
    }

    //! discount from the first exercise date to today, then average
    double df0 = exp( -r * ( tau[1] - tau[0] ) );
    double sum = 0;
    double sum2 = 0;
    for ( size_t p = 0; p < N; p++ )
    {
        double v = cf[p] * df0;
        sum += v;
        sum2 += v * v;
    }
    double continuation0 = sum / N;
    Trust = sqrt( ( sum2 / N - continuation0 * continuation0 ) / ( N - 1 ) );

    //! American value = max( continuation today, immediate exercise at S0 )
    return std::max( continuation0, Contract->PDE_EvalFlow( s0 ) );
}

void PricerMCL::Tree_Read_()
{
    // read results in tree
    MonteCarloNode* N = _root;
    _book->SetPremium( N->GetIndicatorValue( 0 ) );
    _book->SetPremiumTrust( N->GetIndicatorTrust( 0 ) );
    vector<Contract*> option_list = _book->GetOptionList();
    vector<Contract*>::iterator C;
    for ( C = option_list.begin();
          C != option_list.end();
          C++ )
    {
        N = _collector.GetNode( ( *C )->GetName() );
        ( *C )->SetPremium( N->GetIndicatorValue( 0 ) );
        ( *C )->SetPremiumTrust( N->GetIndicatorTrust( 0 ) );
    }

    //! per-bump book premium (single-tree Greeks): read each scenario root's
    //! MC mean, keyed by its tag, for ComputeGreeks_ to finite-difference
    for ( auto& tagged : _scenario_roots )
    {
        _scenario_premium[tagged.first] = tagged.second->GetIndicatorValue( 0 );
    }
}