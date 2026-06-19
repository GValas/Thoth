#include "thoth.hpp"
#include "pricer_mcl.hpp"
#include "cancellation.hpp"
#include "contract.hpp"
#include "vanilla.hpp"
#include "mcl_gpu.hpp"
#include "progress_bar.hpp"
#include "path_generator.hpp"
#include "maths.hpp"
#include <algorithm>
#include <cmath>

namespace
{
//! readable result-field key for a node-graph tree, from the internal scenario tag
//! ("@D+:<s>" delta bump, "@G+:/@G-:<s>" gamma up/down, "@V+" vega, "@R+" rho). The
//! base tree is keyed "premium" by the caller. Output -> "<key>_mcl_graph".
string GraphTreeKey( const string& Tag )
{
    if ( Tag.rfind( "@D+:", 0 ) == 0 )
    {
        return "delta_" + Tag.substr( 4 );
    }
    if ( Tag.rfind( "@G+:", 0 ) == 0 )
    {
        return "gamma_up_" + Tag.substr( 4 );
    }
    if ( Tag.rfind( "@G-:", 0 ) == 0 )
    {
        return "gamma_down_" + Tag.substr( 4 );
    }
    if ( Tag == "@V+" )
    {
        return "vega";
    }
    if ( Tag == "@R+" )
    {
        return "rho";
    }
    return Tag;
}
} // namespace

PricerMCL::PricerMCL( const string& ObjectName,
                      YamlConfig& YamlConfig ) : Pricer( ObjectName, YamlConfig )
{
    //! _rng (xoshiro256++) is seeded per run in SetupQuasiRandom from the config seed
}

PricerMCL::~PricerMCL() = default;

//! diffusion dates
void PricerMCL::InitDates()
{

    //! diffusion dates = fixing_dates + mcl dates + without today
    set<date> fixing_dates = _book->GetFixingDates();
    date last_date = *( --( fixing_dates.end() ) );
    days days_step( _configuration->_mcl->_max_day_step );
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
void PricerMCL::PreCheck()
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

    //! GPU acceleration is opt-in (allow_gpu) and falls back to the CPU path when a
    //! device is absent or the book is not GPU-supported — decided once, here.
    _use_gpu = _configuration->_mcl->_allow_gpu && BookIsGpuSupported();
    if ( _configuration->_mcl->_allow_gpu )
    {
        LOG( "GPU", _use_gpu ? ( "device Monte-Carlo enabled: " + gpu::DeviceInfo() )
                             : ( "allow_gpu set but GPU pricing is unavailable or unsupported for "
                                 "this book — running on the CPU MCL engine (" +
                                 gpu::DeviceInfo() + ")" ) );
    }
}

//! GPU-priceable iff a device is present and every contract is a GPU-supported
//! European vanilla under GBM. All-or-nothing: a mixed book runs on the CPU so the
//! result is never a half-GPU/half-CPU patchwork.
bool PricerMCL::BookIsGpuSupported()
{
    if ( !gpu::Available() )
    {
        return false;
    }
    for ( Contract* c : _book->GetOptionList() )
    {
        GpuGbmParams p;
        if ( !c->GPU_GbmParams( p ) )
        {
            return false;
        }
    }
    return true;
}

bool PricerMCL::GreeksPerContract() const
{
    return _use_gpu;
}

void PricerMCL::PriceContract( Contract* Ctr )
{
    GpuGbmParams p;
    if ( !Ctr->GPU_GbmParams( p ) )
    {
        //! BookIsGpuSupported guarantees every contract is supported in GPU mode
        ERR( "gpu pricing '" + _name + "': contract '" + Ctr->GetName() + "' is not GPU-supported" );
    }

    const long paths = _configuration->_mcl->_paths;
    //! one fixed seed for the base price and every bump -> common random numbers
    const unsigned long seed = (unsigned long)_configuration->_mcl->_seed;

    const gpu::GbmResult r = gpu::PriceEuropeanGbm( p.forward, p.strike, p.t, p.vol,
                                                    p.df, p.is_call, paths, seed );
    Ctr->Result().premium = r.premium;
    Ctr->Result().premium_trust = r.trust;
}

//! price the whole book by Monte-Carlo. Re-runnable: the node tree is rebuilt
//! from the current market each call and the RNG is reseeded, so the Greeks
//! bumps share common random numbers with the base scenario (stable bumps).
void PricerMCL::PriceBook()
{
    //! GPU mode: price contract by contract on the device (per-contract bump
    //! Greeks via the shared PriceBookByContract machinery), then combine the
    //! per-contract Monte-Carlo errors in quadrature (FX-scaled to book currency)
    //! since AggregateContract sums premia but not the trust.
    if ( _use_gpu )
    {
        Pricer::InitPricing();
        PriceBookByContract( "GPU" );
        double var = 0;
        for ( Contract* c : _book->GetOptionList() )
        {
            const double t = c->Result().premium_trust * FxToBook( c );
            var += t * t;
        }
        _book->SetPremiumTrust( std::sqrt( var ) );
        return;
    }

    Pricer::InitPricing();

    //! reset to a clean tree and a fixed RNG seed for reproducible, common-random
    //! pricing across the base and bumped scenarios
    _diffusion_dates.clear();
    _collector.Reset();
    _scenario_roots.clear();
    _scenario_premium.clear();
    _rng.Seed( (std::uint64_t)_configuration->_mcl->_seed );

    //! single-tree Greeks: build the bump sub-trees alongside the base tree (and
    //! price them in the same path sweep) when delta/gamma/vega/rho are requested
    //! and the book supports it. theta is handled separately (reprice), and the
    //! theta reprice / unsupported books set _suppress_scenarios.
    const bool spot_greeks = _request_delta || _request_gamma || _request_vega || _request_rho;
    _build_greek_scenarios = spot_greeks && !_suppress_scenarios && CanSingleTreeGreeks();

    Tree_Init();
    Tree_Run();
    Tree_Read();
    PriceAmerican(); //!< override American contracts via Longstaff-Schwartz
}

//! single-tree Greeks need to isolate one contract's spot bump within a shared
//! path; that requires a plain (Mono) underlying. American early exercise IS
//! supported: each scenario records its own bumped spot path and PriceAmerican
//! re-prices it under the frozen LSM policy (see PriceAmerican). Non-Mono books
//! (basket / composite) still fall back to bump-and-revalue.
bool PricerMCL::CanSingleTreeGreeks() const
{
    for ( Contract* c : _book->GetOptionList() )
    {
        if ( !c->GetUnderlying()->IsMono() )
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
void PricerMCL::BuildGreekScenarios()
{
    //! snapshot the singles first: building a sub-tree must not invalidate the
    //! iterator, and each spot bump is relative to the unbumped spot
    const vector<Single*> singles( _single_set.begin(), _single_set.end() );

    //! BumpsRate/BumpsVol tell the market-data leaves which nodes to mutualise
    //! with the base tree (rate/vol/drift are shared unless this scenario bumps
    //! them), so only the genuinely bumped sub-tree is duplicated.
    auto build = [&]( const string& Tag, bool BumpsRate, bool BumpsVol,
                      const std::function<void()>& Mutate, const std::function<void()>& Restore )
    {
        Mutate();
        _collector.SetScenarioSuffix( Tag );
        _collector.SetScenarioBumps( BumpsRate, BumpsVol );
        MonteCarloNode* root = _book->GetNode( _collector );
        _collector.SetScenarioBumps( false, false );
        _collector.SetScenarioSuffix( "" );
        Restore();
        _scenario_roots.emplace_back( Tag, root );
    };

    //! delta (one-sided, reuses the base price) / gamma (central, needs both
    //! sides). rates and vols are unbumped (shared with the base tree).
    for ( Single* s : singles )
    {
        const double spot = s->GetSpot();
        if ( _request_delta )
        {
            const double h = GREEK_SPOT_BUMP * spot;
            build( "@D+:" + s->GetName(), false, false, [&]
                   { s->SetSpot( spot + h ); }, [&]
                   { s->SetSpot( spot ); } );
        }
        if ( _request_gamma )
        {
            const double h = GREEK_GAMMA_BUMP * spot;
            build( "@G+:" + s->GetName(), false, false, [&]
                   { s->SetSpot( spot + h ); }, [&]
                   { s->SetSpot( spot ); } );
            build( "@G-:" + s->GetName(), false, false, [&]
                   { s->SetSpot( spot - h ); }, [&]
                   { s->SetSpot( spot ); } );
        }
    }

    //! vega : one-sided parallel vol bump on every underlying
    if ( _request_vega )
    {
        build( "@V+", false, true, [&]
               { ApplyVolShift( GREEK_VOL_BUMP ); }, [&]
               { ApplyVolShift( 0 ); } );
    }

    //! rho : one-sided parallel rate bump on every currency's curve
    if ( _request_rho )
    {
        build( "@R+", true, false, [&]
               { ApplyRateShift( GREEK_RATE_BUMP ); }, [&]
               { ApplyRateShift( 0 ); } );
    }
}
//! single-tree Greeks: delta/gamma/vega/rho come from the bump sub-trees priced
//! in the base path sweep (read back as _scenario_premium); theta is a separate
//! reprice (rolling today changes the diffusion-date grid). Unsupported books
//! (American / non-Mono) fall back to the bump-and-revalue base implementation.
void PricerMCL::ComputeGreeks()
{
    if ( !_build_greek_scenarios )
    {
        Pricer::ComputeGreeks(); //!< American / non-Mono : bump-and-revalue
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
            if ( _request_delta ) //!< one-sided forward difference, reuses p0
            {
                const double h = GREEK_SPOT_BUMP * spot;
                delta += ( _scenario_premium.at( "@D+:" + s->GetName() ) - p0 ) / h;
            }
            if ( _request_gamma ) //!< central second difference (needs both sides)
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

    //! vega / rho : one-sided, per 1 vol point / per 1% rate move
    if ( _request_vega )
    {
        _vega = ( _scenario_premium.at( "@V+" ) - p0 ) / GREEK_VOL_BUMP * 0.01;
    }
    if ( _request_rho )
    {
        _rho = ( _scenario_premium.at( "@R+" ) - p0 ) / GREEK_RATE_BUMP * 0.01;
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
            contract_premium.push_back( c->Result().premium );
            contract_trust.push_back( c->Result().premium_trust );
        }

        _quiet_pricing = true; //!< suppress the status lines / node-graph dump...
        _theta_pass = true;    //!< ...but still show a labelled "<engine> theta" bar
        _suppress_scenarios = true;
        _today = base_today + days( 1 );
        _graph_tree_tag = "theta"; //!< capture the rolled tree's node graph
        PriceBook();               //!< the single extra graph
        _graph_tree_tag.clear();
        const double p1 = _book->GetPremium();
        _today = base_today;
        _suppress_scenarios = false;
        _theta_pass = false;
        _quiet_pricing = false;

        //! restore the base scenario (dates + premiums) without repricing
        _book->SetToday( base_today );
        _currency->SetToday( base_today );
        _book->SetPremium( book_premium );
        _book->SetPremiumTrust( book_trust );
        size_t i = 0;
        for ( Contract* c : _book->GetOptionList() )
        {
            c->Result().premium = contract_premium[i];
            c->Result().premium_trust = contract_trust[i];
            i++;
        }

        _theta = p1 - p0;
    }

    _premium = _book->GetPremium();
}

void PricerMCL::CorrelateBrownianNodes()
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
            if ( BrownianNode* B = _collector.GetBrownianNode( ( *u )->GetName() + node_name::BROWNIAN ) )
            {

                //! noise = sum product correl/white_noise
                string node_name = ( *u )->GetName() + node_name::NOISE;
                CorrelatedNoiseNode* correlated_noise_node = _collector.NewNode<CorrelatedNoiseNode>( node_name );

                //! link to other white_noises
                for ( v = _single_set.begin();
                      v != _single_set.end();
                      v++ )
                {

                    double x = _correlation->GetCholeskyValue( ( *u )->GetName(), ( *v )->GetName() );
                    if ( x != 0 )
                    {
                        MonteCarloNode* n = _collector.GetNode( ( *v )->GetName() + node_name::WHITE_NOISE );
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

void PricerMCL::CreateContractualNodes()
{
    _root = _book->GetNode( _collector );
}

void PricerMCL::CreateBrownianNodes()
{

    SingleSet::iterator u;
    for ( u = _single_set.begin();
          u != _single_set.end();
          u++ )
    {
        //! noise
        string node_name = ( *u )->GetName() + node_name::WHITE_NOISE;
        NoiseNode* N = _collector.NewNode<NoiseNode>( node_name );
        N->SetRandomGenerator( &_rng );

        //! brownian — only the local-vol SpotDiffusionNode consumes it; a Heston
        //! spot reads its white-noise directly, so building one for a stochastic-vol
        //! underlying would leave it orphaned (unused node, dead in the DAG/graph).
        if ( !( *u )->GetVolatility()->IsStochastic() )
        {
            node_name = ( *u )->GetName() + node_name::BROWNIAN;
            BrownianNode* B = _collector.NewBrownianNode( node_name );
            B->SetNoiseNode( N );
        }

        //! stochastic vol (Heston) underlyings need a second, independent noise
        //! for the variance process (pseudo-random; Sobol stays on the spot noise)
        if ( ( *u )->GetVolatility()->IsStochastic() )
        {
            NoiseNode* VN = _collector.NewNode<NoiseNode>( ( *u )->GetName() + node_name::VOL_WHITE_NOISE );
            VN->SetRandomGenerator( &_rng );

            //! Bates : an independent compound-Poisson jump source (only created
            //! when the stochastic vol carries jumps; pure Heston has no jump node)
            const StochasticVolParams h = ( *u )->GetVolatility()->StochasticParams();
            if ( h.has_jumps() )
            {
                JumpNode* JN = _collector.NewNode<JumpNode>( ( *u )->GetName() + node_name::JUMP_NOISE );
                JN->SetRandomGenerator( &_rng );
                JN->SetJumpParameters( h.jump_intensity, h.jump_mean, h.jump_vol );
            }
        }
    }
}

//! wire the Sobol + Brownian-bridge generator into the per-underlying noise
//! nodes (only when use_sobol is set); otherwise the nodes keep drawing
//! independent pseudo-random gaussians.
void PricerMCL::SetupQuasiRandom()
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
    //! a cluster slave skips the running path count of the slaves before it
    //! (_sobol_skip, set by the master) so each slave draws a disjoint Sobol block.
    int factors = (int)_single_set.size();
    uint64_t sobol_skip = (uint64_t)_configuration->_mcl->_sobol_skip;
    _path_generator = std::make_unique<PathGenerator>( times, factors, true, &_rng, sobol_skip );

    int f = 0;
    for ( Single* s : _single_set )
    {
        if ( NoiseNode* nn = _collector.GetTypedNode<NoiseNode>( s->GetName() + node_name::WHITE_NOISE ) )
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
        if ( ( *s )->IsForex() )
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
void PricerMCL::Tree_Init()
{
    //! node collector
    InitDates();
    _collector.SetDiffusionDates( _diffusion_dates );

    //! node tree
    CreateBrownianNodes();
    SetupQuasiRandom(); //!< wire Sobol + bridge increments into the noise nodes
    CorrelateBrownianNodes();
    CreateContractualNodes();

    //! base root + (optionally) the Greek bump sub-trees, all sorted into one
    //! schedule so a single path sweep prices price and Greeks together
    vector<MonteCarloNode*> roots{ _root };
    if ( _build_greek_scenarios )
    {
        BuildGreekScenarios();
        for ( auto& tagged : _scenario_roots )
        {
            roots.push_back( tagged.second );
        }
    }

    //! American contracts : register their spot-path recordings BEFORE the sort,
    //! so SortNodes schedules each recorded node (and its dependencies) at every
    //! exercise date — essential for derived spots (composite / basket) that the
    //! flow only references at maturity.
    SetupAmericanRecording();

    //! sort nodes
    _collector.SortNodes( roots );

    //! debug node graph : capture one Graphviz .dot per built tree into the result
    //! block as <tree>_mcl_graph, so it returns to the client over HTTP / in the
    //! batch output — no server-side file. The visible base build captures the
    //! "premium" tree plus any single-tree Greek scenario trees; a quiet Greek
    //! reprice (bump-and-revalue, e.g. composite / basket / American books) captures
    //! its own tree under the tag the bump engine set (delta/gamma/vega/rho/theta).
    if ( _debug && _debug->_generate_nodes_graph )
    {
        if ( !_quiet_pricing )
        {
            _tree_graphs["premium"] = _collector.GraphDot( _root );
            for ( const auto& tagged : _scenario_roots )
            {
                _tree_graphs[GraphTreeKey( tagged.first )] = _collector.GraphDot( tagged.second );
            }
            LOG( LogLabel(), "captured " + std::to_string( _tree_graphs.size() ) + " node graph(s) -> result.<tree>_mcl_graph" );
        }
        else if ( !_graph_tree_tag.empty() )
        {
            _tree_graphs[_graph_tree_tag] = _collector.GraphDot( _root );
        }
    }

    //! status (only on the visible base build; the bump-and-revalue Greek
    //! re-prices set _quiet_pricing, and would otherwise repeat these lines once
    //! per bump)
    if ( !_quiet_pricing )
    {
        {
            std::ostringstream oss;
            oss << _collector.GetNodeNumber() << " created nodes";
            LOG( LogLabel(), oss.str() );
        }
        {
            std::ostringstream oss;
            oss << "diffusion dates = " << _diffusion_dates.size() << ", "
                << "contracts = " << _book->GetOptionList().size() << ", "
                << "underlings = " << _single_set.size() << ", "
                << "currencies = " << _currency_set.size();
            LOG( LogLabel(), oss.str() );
        }
        {
            std::ostringstream oss;
            oss << "drawings = " << _configuration->_mcl->_paths << ", "
                << "max day step = " << _configuration->_mcl->_max_day_step << ", "
                << "vol year step = " << _configuration->_mcl->_vol_year_step;
            LOG( LogLabel(), oss.str() );
        }
    }
}

void PricerMCL::Tree_Run()
{
    //! report resident memory before the (potentially large) path loop starts
    //! (skipped for the silent inner re-prices of bump-and-revalue Greeks)
    if ( !_quiet_pricing )
    {
        string mem = CurrentMemoryUsage();
        LOG( LogLabel(), "starting pricing" + ( mem.empty() ? string() : ", memory = " + mem ) );
    }

    //! iterations
    long n = _configuration->_mcl->_paths;

    //! the bar spans the whole job: the path sweep plus the American LSM fit
    //! (one backward step per exercise date, per American contract). For a plain
    //! European book the LSM part is 0, so the bar is just the sweep.
    const long lsm_steps = AmericanLsmSteps();
    //! the theta one-day reprice runs under _quiet_pricing (no status lines / graph
    //! dump), but we still show its own labelled bar so it is not a silent gap
    //! after the main sweep finishes; it is kept out of GlobalProgress so it does
    //! not make a cluster master's aggregate bar run backwards during that tail.
    const bool show_bar = !_quiet_pricing || _theta_pass;
    const string bar_label = _theta_pass ? LogLabel() + " theta" : LogLabel();
    _progress_bar = std::make_unique<ProgressBar>( bar_label, n + lsm_steps, show_bar, !_theta_pass );

    for ( long i = 0; i < n; i++ )
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
        _progress_bar->Update( i + 1, [&]()
                               { return "price = " + ToString( _root->GetIndicatorValue( 0 ) ) +
                                        ", trust = " + ToString( _root->GetIndicatorTrust( 0 ) ); } );
    }
    _progress_step = n; //!< the LSM fit continues the bar from here

    //! report the recorded American underlying paths (martingale sanity)
    LogRecordings();

    //! European books finish here; American books keep the bar open and finalise
    //! it in PriceAmerican once the LSM premium is known.
    if ( lsm_steps == 0 )
    {
        _progress_bar->Done( "price = " + ToString( _root->GetIndicatorValue( 0 ) ) +
                             ", trust = " + ToString( _root->GetIndicatorTrust( 0 ) ) );
        _progress_bar.reset();
    }
}

//! total backward-induction steps the LSM fit will run across all American
//! contracts (one per interior exercise date) — used to size the progress bar so
//! the American post-pass is embedded in the same bar as the path sweep.
long PricerMCL::AmericanLsmSteps() const
{
    if ( !_collector.IsRecording() )
    {
        return 0;
    }
    long steps = 0;
    for ( Contract* c : _book->GetOptionList() )
    {
        if ( !c->IsAmerican() )
        {
            continue;
        }
        //! FitAmericanPolicy iterates t = M-2 .. 1  ->  M-2 steps (M = grid size)
        long m = (long)_collector.DiffusionIndicesUpTo( c->GetMaturityDate() ).size();
        if ( m > 2 )
        {
            steps += m - 2;
        }
    }
    return steps;
}

//! log / progress-bar label: "AMC" once American contracts are registered for
//! path recording (Longstaff-Schwartz Monte-Carlo), "MCL" for a plain European
//! run. IsRecording() is set in SetupAmericanRecording (early in Tree_Init),
//! before every label site below.
string PricerMCL::LogLabel() const
{
    //! GPU when the book is pricing on the device, else AMC (American LSM) / MCL
    if ( _use_gpu )
    {
        return "GPU";
    }
    return _collector.IsRecording() ? "AMC" : "MCL";
}

//! name of the diffusion node carrying a contract's exercise value. Asking the
//! underlying for its node works for every kind (Mono -> "<name>#spot",
//! composite -> "<eq>_compo_<ccy>#spot", basket -> its own node), unlike the
//! "<underlying-name>#spot" convention which only holds for Mono.
string PricerMCL::AmericanSpotName( Contract* Contract )
{
    MonteCarloNode* spot = Contract->GetUnderlying()->GetNode( _collector );
    return spot ? spot->GetName() : "";
}

//! register the underlying spot of every American contract for path recording
void PricerMCL::SetupAmericanRecording()
{
    size_t n = (size_t)_configuration->_mcl->_paths;
    for ( Contract* c : _book->GetOptionList() )
    {
        if ( !c->IsAmerican() )
        {
            continue;
        }
        //! the exercise-value node (handles composite / basket, not just Mono)
        MonteCarloNode* spot = c->GetUnderlying()->GetNode( _collector );
        if ( !spot )
        {
            continue;
        }
        const string spot_name = spot->GetName();
        vector<size_t> grid = _collector.DiffusionIndicesUpTo( c->GetMaturityDate() );

        //! base spot path : feeds the LSM policy fit and the base premium
        _collector.StartRecording( spot, grid, n );

        //! single-tree Greeks: also record each bump scenario's spot path so the
        //! frozen exercise policy can be applied to the bumped paths (the spot
        //! diffusion node is always duplicated per scenario, so every bump — spot,
        //! vol and rate — exposes its own correctly-bumped path here)
        for ( auto& tagged : _scenario_roots )
        {
            if ( MonteCarloNode* sb = _collector.GetNode( spot_name + tagged.first ) )
            {
                _collector.StartRecording( sb, grid, n );
            }
        }

        std::ostringstream oss;
        oss << "recording " << grid.size() << " exercise dates on '"
            << spot_name << "' for American contract '" << c->GetName() << "'";
        LOG( LogLabel(), oss.str() );
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
        if ( !c->IsAmerican() )
        {
            continue;
        }
        const string spot_name = AmericanSpotName( c );
        const la_matrix* paths = _collector.RecordedPaths( spot_name );
        if ( !paths || paths->size2 == 0 )
        {
            continue;
        }
        size_t last = paths->size2 - 1;
        double mean = 0;
        for ( size_t i = 0; i < paths->size1; i++ )
        {
            mean += la_matrix_get( paths, i, last );
        }
        mean /= paths->size1;
        std::ostringstream oss;
        oss << "recorded '" << spot_name << "' paths : E[S_T] = " << mean
            << " (" << paths->size1 << " x " << paths->size2 << ")";
        LOG( LogLabel(), oss.str() );
    }
}

//! price every American contract by Longstaff-Schwartz, then re-aggregate the
//! book. Single-tree Greeks: the exercise policy is fit ONCE per contract on its
//! base paths and then applied (frozen) to the base paths AND to every bumped
//! scenario's recorded paths, replacing that contract's European contribution in
//! _scenario_premium so ComputeGreeks finite-differences the American values.
void PricerMCL::PriceAmerican()
{
    if ( !_collector.IsRecording() )
    {
        return;
    }

    //! fx factor that converts a contract premium into the book currency
    auto fx_of = [&]( Contract* c ) -> double
    {
        if ( _currency == c->GetPremiumCurrency() )
        {
            return 1.0;
        }
        return _correlation->GetFxSpot( _currency->GetName(),
                                        c->GetPremiumCurrency()->GetName() );
    };

    for ( Contract* c : _book->GetOptionList() )
    {
        if ( !c->IsAmerican() )
        {
            continue;
        }

        const string spot_name = AmericanSpotName( c );
        const la_matrix* S0 = _collector.RecordedPaths( spot_name );
        vector<double> tau = _collector.RecordedTau( spot_name );
        double r = c->GetPremiumCurrency()->GetRate()->GetCurveValue( c->GetMaturityDate() );

        //! fit the exercise policy once on the base paths
        AmericanPolicy policy = FitAmericanPolicy( c, S0, tau, r );

        //! base premium = apply the frozen policy to the base paths
        double trust = 0;
        double premium = ApplyAmericanPolicy( c, S0, tau, r, policy, trust );
        c->Result().premium = premium;
        c->Result().premium_trust = trust;
        std::ostringstream oss;
        oss << "american '" << c->GetName() << "' (LSM, frozen boundary) premium = " << premium;
        LOG( LogLabel(), oss.str() );

        //! re-price each Greek scenario for this contract under the frozen policy,
        //! swapping the contract's European contribution for its American value in
        //! the (book-currency) scenario premium
        const double fx = fx_of( c );
        for ( auto& tagged : _scenario_roots )
        {
            const string& tag = tagged.first;
            auto it = _scenario_premium.find( tag );
            if ( it == _scenario_premium.end() )
            {
                continue;
            }

            //! European contribution of this contract under the bump (to remove)
            MonteCarloNode* euro_node = _collector.GetNode( c->GetName() + tag );
            double euro_c = euro_node ? euro_node->GetIndicatorValue( 0 ) : 0;

            //! bumped paths (spot/vol/rate). The rho scenario also discounts (and
            //! drifts, already in the path) at the bumped rate
            const la_matrix* Sb = _collector.RecordedPaths( spot_name + tag );
            vector<double> taub = _collector.RecordedTau( spot_name + tag );
            if ( !Sb )
            {
                Sb = S0;
                taub = tau;
            }
            double rb = ( tag == "@R+" ) ? r + GREEK_RATE_BUMP : r;

            double tb = 0;
            double amer_c = ApplyAmericanPolicy( c, Sb, taub, rb, policy, tb );

            it->second += ( amer_c - euro_c ) * fx;
        }
    }

    //! re-aggregate the base book premium from the (now American) contracts
    double book_premium = 0;
    for ( Contract* c : _book->GetOptionList() )
    {
        book_premium += c->Result().premium * fx_of( c );
    }
    _book->SetPremium( book_premium );

    //! finalise the shared bar now that the American premium is known (the sweep
    //! left it open). Shows the American book premium, not the European readback.
    if ( _progress_bar )
    {
        _progress_bar->Done( "price = " + ToString( book_premium ) );
        _progress_bar.reset();
    }
}

//! Fit a Longstaff-Schwartz exercise policy on the recorded base paths. Backward
//! induction over the exercise grid; at each interior date the continuation value
//! of the in-the-money paths is regressed on { 1, m, m^2 } (m = S/K, the moneyness
//! normalised by the STRIKE) and the per-date coefficients are stored. Normalising
//! by the strike — where the early-exercise boundary sits — keeps the regressor
//! O(1) around the decision region and better-conditions the { 1, m, m^2 } fit than
//! S/S0 would for deep in/out-of-the-money initial spots. The fitted continuation
//! also drives the cashflow roll-back so earlier dates regress against the realised
//! policy value.
PricerMCL::AmericanPolicy PricerMCL::FitAmericanPolicy( Contract* Contract,
                                                        const la_matrix* Paths,
                                                        const vector<double>& Tau,
                                                        double Rate )
{
    AmericanPolicy pol;
    if ( !Paths || Paths->size2 < 2 )
    {
        return pol;
    }

    //! minimum in-the-money paths required before fitting the {1, m, m^2}
    //! continuation regression: with only a handful the 3-parameter fit is exactly
    //! determined (interpolation, not regression) and yields unreliable
    //! continuation values, so demand several observations per basis function.
    constexpr size_t MIN_ITM_FOR_REGRESSION = 50;

    size_t N = Paths->size1; //!< paths
    size_t M = Paths->size2; //!< columns: tau[0]=0 (today) .. tau[M-1]=maturity
    //! moneyness normaliser: the strike (the exercise boundary sits near it) for a
    //! vanilla, falling back to the base-path initial spot for any other contract
    pol.basis_norm = la_matrix_get( Paths, 0, 0 ); //!< initial spot (fallback)
    if ( Vanilla* van = dynamic_cast<Vanilla*>( Contract ) )
    {
        if ( van->GetStrike() > 0 )
        {
            pol.basis_norm = van->GetStrike();
        }
    }
    pol.tau = Tau;
    pol.b0.assign( M, 0.0 );
    pol.b1.assign( M, 0.0 );
    pol.b2.assign( M, 0.0 );
    pol.has_fit.assign( M, false );

    //! cashflow per path, expressed as value at the current backward time step
    vector<double> cf( N );
    for ( size_t p = 0; p < N; p++ )
    {
        cf[p] = Contract->Intrinsic( la_matrix_get( Paths, p, M - 1 ) ); //!< at maturity
    }

    //! backward induction over the interior exercise dates
    for ( int t = (int)M - 2; t >= 1; t-- )
    {
        //! advance the shared progress bar: the LSM fit continues the same bar
        //! the path sweep started, so the whole American job fills one bar
        if ( _progress_bar )
        {
            _progress_bar->Update( ++_progress_step );
        }

        double df = exp( -Rate * ( Tau[t + 1] - Tau[t] ) );
        for ( size_t p = 0; p < N; p++ )
        {
            cf[p] *= df; //!< discount future cashflow to this date
        }

        //! in-the-money paths
        vector<size_t> itm;
        for ( size_t p = 0; p < N; p++ )
        {
            if ( Contract->Intrinsic( la_matrix_get( Paths, p, t ) ) > 0 )
            {
                itm.push_back( p );
            }
        }
        if ( itm.size() < MIN_ITM_FOR_REGRESSION )
        {
            continue; //!< too few points for a meaningful fit -> hold at this date
        }

        //! regress discounted continuation cashflow on { 1, m, m^2 }
        size_t ni = itm.size();
        LaMatrix X = la_matrix_alloc( ni, 3 ); //!< RAII: freed on every exit (incl. throw)
        LaVector y = la_vector_alloc( ni );
        for ( size_t k = 0; k < ni; k++ )
        {
            double m = la_matrix_get( Paths, itm[k], t ) / pol.basis_norm;
            la_matrix_set( X, k, 0, 1.0 );
            la_matrix_set( X, k, 1, m );
            la_matrix_set( X, k, 2, m * m );
            la_vector_set( y, k, cf[itm[k]] );
        }
        vector<double> beta = LeastSquares( X, y );
        if ( beta.empty() )
        {
            continue; //!< singular regression at this date -> hold (has_fit stays false)
        }
        pol.b0[t] = beta[0];
        pol.b1[t] = beta[1];
        pol.b2[t] = beta[2];
        pol.has_fit[t] = true;

        //! roll the cashflow back through the fitted exercise decision
        for ( size_t p : itm )
        {
            double s = la_matrix_get( Paths, p, t );
            double m = s / pol.basis_norm;
            double continuation = pol.b0[t] + pol.b1[t] * m + pol.b2[t] * m * m;
            double intrinsic = Contract->Intrinsic( s );
            if ( intrinsic >= continuation )
            {
                cf[p] = intrinsic;
            }
        }
        //! X, y are RAII-freed at scope exit
    }

    return pol;
}

//! Apply a fitted (frozen) exercise policy to a set of paths: walk each path
//! forward and exercise at the first interior date whose intrinsic beats the
//! frozen continuation estimate, otherwise take the maturity payoff. Returns the
//! discounted MC mean (vs immediate exercise at the path-set's initial spot).
//! Rate is the scenario's discount rate (bumped for rho); the moneyness is always
//! normalised by the policy's basis_norm (the strike) so the frozen boundary stays
//! comparable across the base and bumped path sets.
double PricerMCL::ApplyAmericanPolicy( Contract* Contract,
                                       const la_matrix* Paths,
                                       const vector<double>& Tau,
                                       double Rate,
                                       const AmericanPolicy& Policy,
                                       double& Trust )
{
    Trust = 0;
    if ( !Paths || Paths->size2 < 2 )
    {
        return 0;
    }

    size_t N = Paths->size1; //!< paths
    size_t M = Paths->size2; //!< columns: tau[0]=0 (today) .. tau[M-1]=maturity
    double sum = 0;
    double sum2 = 0;
    for ( size_t p = 0; p < N; p++ )
    {
        double value = 0; //!< discounted-to-today cashflow of this path
        for ( size_t t = 1; t < M; t++ )
        {
            double s = la_matrix_get( Paths, p, t );
            double intrinsic = Contract->Intrinsic( s );

            if ( t == M - 1 ) //!< maturity : forced exercise (take the payoff)
            {
                value = intrinsic * exp( -Rate * ( Tau[t] - Tau[0] ) );
                break;
            }

            //! interior date : exercise when intrinsic beats the frozen continuation
            if ( intrinsic > 0 && Policy.has_fit[t] )
            {
                double m = s / Policy.basis_norm;
                double continuation = Policy.b0[t] + Policy.b1[t] * m + Policy.b2[t] * m * m;
                if ( intrinsic >= continuation )
                {
                    value = intrinsic * exp( -Rate * ( Tau[t] - Tau[0] ) );
                    break;
                }
            }
        }
        sum += value;
        sum2 += value * value;
    }

    double mean = sum / N;
    Trust = sqrt( ( sum2 / N - mean * mean ) / ( N - 1 ) );

    //! American value = max( continuation today, immediate exercise at the
    //! path-set's initial spot ) — for bumped paths this is the bumped spot
    double s0_set = la_matrix_get( Paths, 0, 0 );
    return std::max( mean, Contract->Intrinsic( s0_set ) );
}

void PricerMCL::Tree_Read()
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
        ( *C )->Result().premium = N->GetIndicatorValue( 0 );
        ( *C )->Result().premium_trust = N->GetIndicatorTrust( 0 );
    }

    //! per-bump book premium (single-tree Greeks): read each scenario root's
    //! MC mean, keyed by its tag, for ComputeGreeks to finite-difference
    for ( auto& tagged : _scenario_roots )
    {
        _scenario_premium[tagged.first] = tagged.second->GetIndicatorValue( 0 );
    }
}