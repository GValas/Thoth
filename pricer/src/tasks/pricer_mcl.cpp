#include "thoth.hpp"
#include "pricer_mcl.hpp"
#include "cancellation.hpp"
#include "contract.hpp"
#include "enums.hpp"
#include "vanilla.hpp"
#include "mcl_gpu.hpp"
#include "object_reader.hpp"
#include "progress_bar.hpp"
#include "path_generator.hpp"
#include "sobol_generator.hpp" //!< MaxDimension (the Sobol budget log)
#include "single.hpp"          //!< Volatility / IsStochastic (MonoVol helper for the GPU gate)
#include "maths.hpp"
#include <algorithm>
#include <cmath>
#include "mcl_scenario_tag.hpp"

namespace
{

//! readable result-field key for a node-graph tree, from the internal scenario tag
//! ("@D+:<s>" delta bump, "@G+:/@G-:<s>" gamma up/down, "@V+" vega, "@R+" rho). The
//! base tree is keyed "premium" by the caller. Output -> "<key>_mcl_graph".
string GraphTreeKey( const string& Tag )
{
    using namespace scenario_tag;
    if ( Tag.starts_with( DELTA_PREFIX ) )
    {
        return "delta_" + Tag.substr( sizeof( DELTA_PREFIX ) - 1 );
    }
    if ( Tag.starts_with( GAMMA_UP_PREFIX ) )
    {
        return "gamma_up_" + Tag.substr( sizeof( GAMMA_UP_PREFIX ) - 1 );
    }
    if ( Tag.starts_with( GAMMA_DOWN_PREFIX ) )
    {
        return "gamma_down_" + Tag.substr( sizeof( GAMMA_DOWN_PREFIX ) - 1 );
    }
    if ( Tag == VEGA )
    {
        return "vega";
    }
    if ( Tag == RHO )
    {
        return "rho";
    }
    return Tag;
}

} // namespace

//! the diffusion tree, node collector and path generator are all built lazily in
//! PriceBook (so each re-price gets a clean tree); only the RNG is held as a member
PricerMCL::PricerMCL( const string& ObjectName,
                      YamlConfig& YamlConfig ) : Pricer( ObjectName, YamlConfig, KIND_MCL_PRICER, "GPU" )
{
    //! _rng (xoshiro256++) is seeded per run in SetupQuasiRandom from the config seed
}

//! unique_ptr members (progress bar, path generator) clean themselves up
PricerMCL::~PricerMCL() = default;

//! common pricer fields first, then this engine's required parameter object
void PricerMCL::Configure( ObjectReader& reader )
{
    Pricer::Configure( reader );
    _mcl = reader.Ref<MclConfiguration>( "mcl_configuration" ); //!< Monte-Carlo grid / paths / GPU
}

//! diffusion dates
void PricerMCL::InitDates()
{

    //! diffusion dates = fixing_dates + mcl dates + without today
    set<date> fixing_dates = _book->GetFixingDates();
    date last_date = *( --( fixing_dates.end() ) );

    //! diffusion-grid resolution: the grid is stepped at max_day_step, but
    //! vol_year_step (a year fraction) caps the step further so the variance
    //! process is sub-stepped — Andersen-QE bias grows with the step size, so a
    //! long max_day_step (e.g. 30d) under strong mean-reversion would otherwise
    //! accumulate variance-path bias. Refining the shared grid (rather than only
    //! the variance node) keeps every sub-step's spot/variance Brownians
    //! correlated and Sobol-drawn, and keeps the same grid across Greek bumps
    //! (common random numbers). Applied only when the book carries a genuine
    //! stochastic-variance model (Heston / Bates): a plain GBM book has no
    //! variance path to sub-step, so it stays on max_day_step (cost unchanged).
    //! vol_year_step <= 0 disables the cap.
    long step_days = _mcl->_max_day_step;
    if ( _mcl->_vol_year_step > 0 )
    {
        bool has_stochastic_vol = false;
        for ( Single* s : _single_set )
        {
            if ( s->GetVolatility()->IsStochastic() )
            {
                has_stochastic_vol = true;
                break;
            }
        }
        if ( has_stochastic_vol )
        {
            const long vol_days = std::max<long>(
                1, (long)std::floor( _mcl->_vol_year_step * NB_OF_DAYS_A_YEAR ) );
            step_days = std::min( step_days, vol_days );
        }
    }
    days days_step( step_days );
    for ( date d = _today;
          d < last_date;
          d += days_step )
    {
        _diffusion_dates.insert( d );
    }
    _diffusion_dates.insert( fixing_dates.begin(), fixing_dates.end() );
    //_diffusion_dates.erase( _today );
}

//! check the correlation is present (the mcl_configuration is resolved as a
//! required reference in Configure, so it is always non-null here)
void PricerMCL::PreCheck()
{
    //! MCL diffusion correlates the underlyings' Brownian motions, so a
    //! correlation matrix is mandatory (guards the unconditional derefs in
    //! ComputeCholeskyMatrix / CorrelateBrownianNodes below).
    if ( !_correlation )
    {
        ERR( "book pricing '" + _name + "' requires a correlation matrix" );
    }

    //! stochastic rates (Hull-White): the supported scope is a same-currency book
    //! of European contracts on BS-vol equities. FX under stochastic rates, quanto
    //! drifts, local/stochastic vol mixing and the deterministic-discounting LSM
    //! American pass are all out of scope — fail loudly rather than misprice.
    bool hybrid = false;
    for ( Currency* c : _book->GetCurrencySet() )
    {
        hybrid |= ( c->GetRateModel() != nullptr );
    }
    if ( hybrid )
    {
        for ( Single* s : _book->GetSingleSet() )
        {
            if ( s->IsForex() )
            {
                ERR( "book pricing '" + _name + "' : FX factor '" + s->GetName() +
                     "' is not supported under stochastic rates (hull_white)" );
            }
            if ( s->GetVolatility()->IsStochastic() || s->GetVolatility()->_is_local )
            {
                ERR( "book pricing '" + _name + "' : underlying '" + s->GetName() +
                     "' must carry a bs_volatility under stochastic rates (hull_white)" );
            }
        }
        for ( Contract* c : _book->GetContractSet() )
        {
            if ( c->IsAmerican() )
            {
                ERR( "book pricing '" + _name + "' : American contract '" + c->GetName() +
                     "' is not supported under stochastic rates (LSM discounting is "
                     "deterministic)" );
            }
            if ( c->GetPremiumCurrency() != c->GetUnderlying()->GetCurrency() ||
                 c->GetPremiumCurrency() != _currency )
            {
                ERR( "book pricing '" + _name + "' : contract '" + c->GetName() +
                     "' must settle in the (single) book currency under stochastic "
                     "rates (quanto / cross-currency hybrids are not supported)" );
            }
        }
    }

    //! GPU acceleration is opt-in (allow_gpu) and falls back to the CPU path when a
    //! device is absent or the book is not GPU-supported — decided once, here.
    _use_gpu = _mcl->_allow_gpu && BookIsGpuSupported();
    if ( _mcl->_allow_gpu )
    {
        LOG( "GPU", _use_gpu ? ( "device Monte-Carlo enabled: " + gpu::DeviceInfo() )
                             : ( "allow_gpu set but GPU pricing is unavailable or unsupported for "
                                 "this book — running on the CPU MCL engine (" +
                                 gpu::DeviceInfo() + ")" ) );
    }
}

//! per-contract Greeks are available either from the GPU per-contract loop, or once
//! the CPU single-tree sweep has attributed its bump scenarios per contract
//! (ComputeGreeks sets _per_contract_greeks_ready). Before that — and on the book-level
//! fallback path — only the book Greeks exist, so this stays false there.
bool PricerMCL::GreeksPerContract() const
{
    return _use_gpu || _per_contract_greeks_ready;
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
        PriceBookByContract();
        double var = 0;
        for ( Contract* c : _book->GetContractSet() )
        {
            const double t = Result( c ).premium_trust * FxToBook( c );
            var += t * t;
        }
        _book_result.premium_trust = std::sqrt( var );
        return;
    }

    Pricer::InitPricing();

    //! reset to a clean tree and a fixed RNG seed for reproducible, common-random
    //! pricing across the base and bumped scenarios
    _diffusion_dates.clear();
    _collector.Reset();
    _recorder.Clear(); //!< drop American path recordings alongside the node graph
    _scenario_roots.clear();
    _scenario_premium.clear();
    _contract_scenario_premium.clear();
    _rng.Seed( (std::uint64_t)_mcl->_seed );

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

//! turn the per-underlying independent white noises into correlated Brownian
//! increments: each underlying's Brownian draws a CorrelatedNoiseNode that is the
//! Cholesky-weighted sum of all the white noises (L * Z), reproducing the target
//! correlation matrix. Single-asset books skip this (no correlation to impose).
void PricerMCL::CorrelateBrownianNodes()
{
    //! the correlated factor set: the singles plus one "<ccy>_ir" rate factor per
    //! Hull-White currency (whose OU node consumes its correlated noise directly —
    //! there is no Brownian for a rate factor)
    vector<string> factor_names;
    set<string> rate_factor_names;
    for ( Single* s : _single_set )
    {
        factor_names.push_back( s->GetName() );
    }
    for ( Currency* c : _currency_set )
    {
        if ( c->GetRateModel() )
        {
            factor_names.push_back( c->IrFactorName() );
            rate_factor_names.insert( c->IrFactorName() );
        }
    }

    //! more than 1 factor : correlation of noises
    if ( factor_names.size() > 1 )
    {

        //! compute cholesky matrix : L with L L^T = correlation (so L*Z is correlated)
        ComputeCholeskyMatrix();

        //! correlated noises = sum product. A factor gets its CorrelatedNoiseNode
        //! "<name>#noise" when something consumes it: a single's Brownian (below),
        //! or a rate factor's HullWhiteFactorNode (fetches it by name). A Heston
        //! spot reads its white noise directly, so it gets none (as before).
        for ( const string& u : factor_names )
        {
            BrownianNode* B = _collector.GetBrownianNode( u + node_name::BROWNIAN );
            if ( !B && !rate_factor_names.count( u ) )
            {
                continue;
            }

            //! noise = sum product correl/white_noise
            CorrelatedNoiseNode* correlated_noise_node =
                _collector.NewNode<CorrelatedNoiseNode>( u + node_name::NOISE );

            //! correlated noise for u = sum_v L[u][v] * white_noise[v]; the
            //! Cholesky factor is lower-triangular, so the zero entries above the
            //! diagonal are skipped (no node wired for them). For a term-structured
            //! correlation the factor varies by date, so the whole lower triangle
            //! is wired (CholeskyMayBeNonZero folds both rules).
            for ( const string& v : factor_names )
            {
                if ( _correlation->CholeskyMayBeNonZero( u, v ) )
                {
                    MonteCarloNode* n = _collector.GetNode( v + node_name::WHITE_NOISE );
                    MonteCarloNode* c = _correlation->GetCholeskyNode( _collector, u,
                                                                       v ); // constant, or per-step for a term structure
                    correlated_noise_node->PushNoiseNode( n );
                    correlated_noise_node->PushCholeskyNode( c );
                }
            }

            //! link to brownian (a rate factor has no Brownian: its OU node
            //! fetches the correlated noise by name instead)
            if ( B )
            {
                B->SetNoiseNode( correlated_noise_node );
            }
        }
    }
}

//! build the payoff side of the DAG: ask the book for its node, which recursively
//! builds each contract's payoff / diffusion nodes on top of the Brownian nodes
//! created above. The returned node is the book root whose indicator 0 is the price.
void PricerMCL::CreateContractualNodes()
{
    _root = _book->GetNode( _collector );
}

//! create the random-source leaves of the DAG: one white-noise node per underlying
//! (plus, for stochastic-vol, an independent variance noise and, for Bates, a jump
//! source), and a Brownian node for the local-vol underlyings that consume one.
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
        //! for the variance process (Sobol-driven like the spot noise — its factor
        //! is appended after the singles in SetupQuasiRandom; the Bates JUMP noise
        //! below stays pseudo-random: a compound-Poisson draw consumes a variable
        //! number of uniforms, incompatible with a fixed Sobol dimension)
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

    //! stochastic rates: one white noise per Hull-White currency ("<ccy>_ir"),
    //! appended after the singles so their Sobol dimensions are unchanged
    for ( Currency* c : _currency_set )
    {
        if ( c->GetRateModel() )
        {
            NoiseNode* N = _collector.NewNode<NoiseNode>( c->IrFactorName() + node_name::WHITE_NOISE );
            N->SetRandomGenerator( &_rng );
        }
    }
}

//! wire the Sobol + Brownian-bridge generator into the per-underlying noise
//! nodes (only when use_sobol is set); otherwise the nodes keep drawing
//! independent pseudo-random gaussians.
void PricerMCL::SetupQuasiRandom()
{
    if ( !_mcl->_use_sobol )
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

    //! the Sobol-driven factors, name-ordered for reproducibility. The spot
    //! noises come FIRST — a pure-BS book's dimension layout (and results) is
    //! bit-unchanged — then the Heston/Bates variance noises and the Hull-White
    //! rate noises are APPENDED: they are per-date N(0,1) streams like the spot
    //! ones, and the Brownian bridge is a measure-preserving reordering (the low
    //! Sobol dimensions drive the coarse shape of each driver path, which
    //! dominates the integrated variance / discounting), so extending the QMC
    //! treatment to them is both correct and variance-reducing. The Bates JUMP
    //! noise stays pseudo-random: its compound-Poisson draw consumes a variable
    //! number of uniforms per path, incompatible with a fixed Sobol dimension.
    //! A cluster slave skips the running path count of the slaves before it
    //! (_sobol_skip, set by the master) so each slave draws a disjoint Sobol block.
    vector<string> factor_noise_names;
    for ( Single* s : _single_set )
    {
        factor_noise_names.push_back( s->GetName() + node_name::WHITE_NOISE );
    }
    for ( Single* s : _single_set )
    {
        if ( s->GetVolatility()->IsStochastic() )
        {
            factor_noise_names.push_back( s->GetName() + node_name::VOL_WHITE_NOISE );
        }
    }
    for ( Currency* c : _currency_set )
    {
        if ( c->GetRateModel() )
        {
            factor_noise_names.push_back( c->IrFactorName() + node_name::WHITE_NOISE );
        }
    }

    const int factors = (int)factor_noise_names.size();
    //! the Joe-Kuo table caps the dimension budget; beyond it the tail steps
    //! silently fall back to pseudo-random inside the generator — say so once
    if ( times.size() * factors > SobolGenerator::MaxDimension() )
    {
        LOG( "MCL", "Sobol dimension budget exceeded (" + std::to_string( times.size() * factors ) +
                        " > " + std::to_string( SobolGenerator::MaxDimension() ) +
                        "): the finest increments fall back to pseudo-random" );
    }
    uint64_t sobol_skip = (uint64_t)_mcl->_sobol_skip;
    _path_generator = std::make_unique<PathGenerator>( times, factors, true, &_rng, sobol_skip );

    int f = 0;
    for ( const string& noise_name : factor_noise_names )
    {
        if ( NoiseNode* nn = _collector.GetTypedNode<NoiseNode>( noise_name ) )
        {
            nn->SetNoiseBuffer( _path_generator->Buffer( f ) );
        }
        f++;
    }
}

//! factorise the correlation matrix over the diffused underlyings (L L^T = C), in a
//! deterministic name order so the resulting Cholesky factor — and hence the
//! correlated draws — are reproducible across runs and Greek bumps. Equity / index
//! names are ordered before the FX names so the FX block sits last (its triangular
//! dependence on the equity block is then well defined).
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
            fx_name_list.push_back( ( *s )->GetName() ); //!< FX names go last
        }
        else
        {
            single_name_list.push_back( ( *s )->GetName() );
        }
    }
    single_name_list.insert( single_name_list.end(), fx_name_list.begin(), fx_name_list.end() );
    //! Hull-White rate factors join last ("<ccy>_ir" must be listed in the
    //! matrix's underlyings, like the Heston "<name>_var" convention)
    for ( Currency* c : _currency_set )
    {
        if ( c->GetRateModel() )
        {
            single_name_list.push_back( c->IrFactorName() );
        }
    }
    _correlation->ComputeCholeskyMatrix( single_name_list );
}

//! build the whole node DAG for one PriceBook call: diffusion dates, the noise /
//! Brownian leaves, their correlation, the contract payoff nodes, the optional
//! Greek-bump sub-trees and American recordings — then topologically sort every
//! root into one schedule so a single path sweep prices price and Greeks together.
//! Also captures the debug node graph and logs the build summary.
void PricerMCL::Tree_Init()
{
    //! node collector : establish the diffusion-date grid the nodes are scheduled on
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

    //! sort nodes — pass the recorder's columns so a derived spot's interior path
    //! dates are scheduled, not just where the contract flow reaches it
    _collector.SortNodes( roots, _recorder.SchedulePoints() );

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
                << "contracts = " << _book->GetContractSet().size() << ", "
                << "underlings = " << _single_set.size() << ", "
                << "currencies = " << _currency_set.size();
            LOG( LogLabel(), oss.str() );
        }
        {
            std::ostringstream oss;
            oss << "drawings = " << _mcl->_paths << ", "
                << "max day step = " << _mcl->_max_day_step << ", "
                << "vol year step = " << _mcl->_vol_year_step;
            LOG( LogLabel(), oss.str() );
        }
    }
}

//! MCL-only result fields (called from Pricer::WriteResults after the premium): one
//! Graphviz .dot per captured MC tree, emitted as <tree>_mcl_graph. _tree_graphs is
//! empty unless debug generate_nodes_graph is on, so this is a no-op otherwise.
void PricerMCL::WriteEngineResults()
{
    for ( const auto& [tree, dot] : _tree_graphs )
    {
        WriteResult( tree + "_mcl_graph", dot );
    }
}

//! the Monte-Carlo path sweep: draw `paths` correlated paths, evaluate the whole
//! scheduled DAG on each (PriceNodes accumulates the running mean / variance into
//! every indicator node) and record the American spot paths. Drives the shared
//! progress bar, which it leaves open for the American LSM post-pass to finish.
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
    long n = _mcl->_paths;

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
        _recorder.RecordPath();

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
    return _recorder.IsRecording() ? "AMC" : "MCL";
}

//! harvest the swept results: the book root's indicator 0 is the MC mean premium
//! (trust = its standard error); each contract node carries its own; and each Greek
//! scenario root carries the bumped book premium for ComputeGreeks to difference.
void PricerMCL::Tree_Read()
{
    // read results in tree : book root indicator 0 = mean premium, trust = std error
    MonteCarloNode* N = _root;
    _book_result.premium = N->GetIndicatorValue( 0 );
    _book_result.premium_trust = N->GetIndicatorTrust( 0 );
    for ( Contract* c : _book->GetContractSet() )
    {
        N = _collector.GetNode( c->GetName() );
        Result( c ).premium = N->GetIndicatorValue( 0 );
        Result( c ).premium_trust = N->GetIndicatorTrust( 0 );
    }

    //! per-bump book premium (single-tree Greeks): read each scenario root's
    //! MC mean, keyed by its tag, for ComputeGreeks to finite-difference
    for ( auto& tagged : _scenario_roots )
    {
        const string& tag = tagged.first;
        _scenario_premium[tag] = tagged.second->GetIndicatorValue( 0 );

        //! and the SAME bump's premium for each individual contract (its scenario node
        //! is "<contract><tag>", in the contract's own currency), so ComputeGreeks can
        //! attribute the Greek per contract. A contract the bump does not touch shares
        //! the unbumped sub-tree, so its value equals the base — a zero contribution.
        for ( Contract* c : _book->GetContractSet() )
        {
            if ( MonteCarloNode* cn = _collector.GetNode( c->GetName() + tag ) )
            {
                _contract_scenario_premium[c->GetName()][tag] = cn->GetIndicatorValue( 0 );
            }
        }
    }
}