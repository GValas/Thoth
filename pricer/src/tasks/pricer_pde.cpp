#include <format>
#include "thoth.hpp"
#include "maths.hpp"
#include "pricer_pde.hpp"
#include "barrier.hpp"
#include "cancellation.hpp"
#include "object_reader.hpp"
#include "progress_bar.hpp"
#include "autocallable.hpp"
#include "variance_swap.hpp"
#include "vanilla.hpp"
#include "digital.hpp"

//! Heston / Bates 2-D (S, v) ADI grid parameters (see SolveHestonGrid). The grid
//! resolution follows the book's vanilla_precision (low | medium | high): a 2-D ADI
//! grid cannot use the 1-D vanilla node counts (1501x1301) — the (NS x Nv x Nt)
//! product would be billions of nodes — so a dedicated, coarser triplet is selected
//! per precision level. Medium reproduces the historical 120/60/120 default; high
//! refines it for long-dated contracts where the coarse grid was too imprecise.
static constexpr int HESTON_GRID_NS_LOW = 80, HESTON_GRID_NV_LOW = 40, HESTON_GRID_NT_LOW = 80;
static constexpr int HESTON_GRID_NS_MEDIUM = 120, HESTON_GRID_NV_MEDIUM = 60, HESTON_GRID_NT_MEDIUM = 120;
static constexpr int HESTON_GRID_NS_HIGH = 240, HESTON_GRID_NV_HIGH = 120, HESTON_GRID_NT_HIGH = 240;
static constexpr double HESTON_VMAX_FACTOR = 6.0;       //!< variance domain = max(1, factor * max(v0, theta))
static constexpr double HESTON_SMAX_MIN_FACTOR = 3.0;   //!< spot domain is at least factor * S0 ...
static constexpr double HESTON_SMAX_SIGMA_FACTOR = 5.0; //!< ... or S0 * exp(factor * sqrt(var * T)), whichever is larger
static constexpr int BATES_JUMP_QUAD_POINTS = 40;       //!< trapezoid points for the jump-size integral
static constexpr double BATES_JUMP_SIGMA_SPAN = 6.0;    //!< jump-size grid spans muJ +/- span * sigJ

//! a finite-difference pricer is just a Pricer; the grid state is built per
//! contract in InitGrid, so nothing to initialise here
PricerPDE::PricerPDE( const string& ObjectName,
                      YamlConfig& YamlConfig ) : Pricer( ObjectName, YamlConfig, KIND_PDE_PRICER, "PDE" )
{
}

PricerPDE::~PricerPDE() = default;

//! common pricer fields first, then this engine's required parameter object
void PricerPDE::Configure( ObjectReader& reader )
{
    Pricer::Configure( reader );
    _pde = reader.Ref<PdeConfiguration>( "pde_configuration" ); //!< grid precision / sizes
}

//! read the option value at the transformed coordinate x off a solved grid layer Uy
//! by cubic-spline interpolation across the (uniform-in-x) grid nodes. Used to
//! evaluate the premium / bumped spots at x_0 and at the central-difference spot
//! bumps, which fall between grid nodes after the sinh coordinate change.
double PricerPDE::GetGridPrice( double x, la_vector* Uy )
{
    // x_vector (owned locally, freed on return)
    LaVector Ux = la_vector_alloc( Uy->size );
    for ( int j = 0; j < (int)Uy->size; j++ )
    {
        la_vector_set( Ux, j, j * _h );
    }

    // interpolation (inputs stay owned here)
    return InterpolateWithSpline( Ux, Uy, x );
}

//! check that PDE resolution is allowed for every contract (the pde_configuration
//! is resolved as a required reference in Configure, so it is always non-null here)
void PricerPDE::PreCheck()
{
    //! stochastic rates (Hull-White) would need a 2-D (S, r) ADI grid — not
    //! implemented yet (see TODO); MCL and ANA cover the hybrid scope.
    for ( Currency* c : _book->GetCurrencySet() )
    {
        if ( c->GetRateModel() )
        {
            ERR( "book pricing '" + _name + "' : the PDE engine does not support "
                                            "stochastic rates (hull_white on '" +
                 c->GetName() + "') yet" );
        }
    }

    for ( Contract* c : _book->GetContractSet() )
    {
        //! the PDE grid handles a vanilla, a barrier or a variance swap on a
        //! griddable underlying; dispatch on the type (RTTI) rather than the kind
        //! string, mirroring PriceContract / PricerANA::PreCheck.
        const bool supported = dynamic_cast<Vanilla*>( c ) || dynamic_cast<Digital*>( c ) ||
                               dynamic_cast<Barrier*>( c ) || dynamic_cast<VarianceSwap*>( c ) ||
                               dynamic_cast<Autocallable*>( c );

        if ( !c->GetUnderlying()->IsGriddable() || !supported )
        {
            std::string msg = std::format( "{} ({}) can't be PDE-priced", c->GetName(), c->GetKind() );
            ERR( msg );
        }

        //! Phoenix coupon MEMORY is path-dependent (the missed-coupon count is
        //! not a function of the spot alone): the 1-D backward grid cannot carry
        //! it without a state augmentation — MCL prices it pathwise instead.
        if ( auto* ac = dynamic_cast<Autocallable*>( c ); ac && ac->HasCouponMemory() )
        {
            ERR( c->GetName() + " (autocallable) : the coupon_memory flavour needs the "
                                "MCL engine (a 1-D grid cannot carry the missed-coupon state)" );
        }
    }
}

//! solve the grid for every contract (re-runnable for the Greeks bumps)
void PricerPDE::PriceBook()
{
    PriceBookByContract();
}

//! single-contract grid solve hook used by the per-contract loop and its Greeks:
//! dispatch on the contract type to the matching grid solve.
void PricerPDE::PriceContract( Contract* Ctr )
{

    if ( auto* vs = dynamic_cast<VarianceSwap*>( Ctr ) )
    {
        SolveVarianceSwap( vs ); //!< expected-accumulated-variance grid (sets premium)
    }
    else if ( auto* ac = dynamic_cast<Autocallable*>( Ctr ) )
    {
        //! backward induction with the autocall overwrites set up by InitGrid
        InitGrid( ac, false );
        StoreResult( ac, SolveGrid( ac ) );
    }
    else if ( auto* bar = dynamic_cast<Barrier*>( Ctr ) )
    {
        PriceBarrier( bar );
    }
    else if ( auto* van = dynamic_cast<Vanilla*>( Ctr ) )
    {
        PriceVanilla( van );
    }
    else if ( auto* dig = dynamic_cast<Digital*>( Ctr ) )
    {
        //! a digital is a European terminal-payoff contract — the same full-domain grid
        //! solve as a vanilla, its (binary) payoff taken from Contract::Intrinsic.
        PriceVanilla( dig );
    }
    else
    {
        ERR( "Unsupported contract : " + Ctr->GetName() );
    }
}

//! plain / Heston vanilla (no barrier): a single full-domain grid solve.
void PricerPDE::PriceVanilla( Contract* Ctr )
{
    //! Heston stochastic vol : 2-D (S, v) ADI grid; otherwise the 1-D spot grid
    if ( UnderlyingIsHeston( Ctr ) )
    {
        StoreResult( Ctr, SolveHestonGrid( Ctr ) );
        return;
    }
    InitGrid( Ctr, false );
    StoreResult( Ctr, SolveGrid( Ctr ) );
}

//! knock-in by in/out parity (vanilla - knock-out); a knock-out returns KnockOut as is.
PricerPDE::GridResult PricerPDE::KnockInParity( Barrier* Ctr, const GridResult& KnockOut )
{
    if ( !Ctr->IsIn() )
    {
        return KnockOut;
    }
    InitGrid( Ctr, false ); //!< the unbarriered vanilla
    GridResult vanilla = SolveGrid( Ctr );
    GridResult res;
    res.premium = vanilla.premium - KnockOut.premium;
    res.delta = vanilla.delta - KnockOut.delta;
    res.gamma = vanilla.gamma - KnockOut.gamma;
    return res;
}

//! barrier: solve the knock-out, then knock-in by in/out parity. Continuous
//! monitoring shortcuts a barrier already breached at valuation.
void PricerPDE::PriceBarrier( Barrier* Ctr )
{
    //! discrete monitoring : full-domain solve, the knocked region zeroed only at the
    //! scheduled dates (monitoring starts after today, so no initial-breach shortcut).
    if ( Ctr->IsDiscrete() )
    {
        InitGrid( Ctr, true );
        StoreResult( Ctr, KnockInParity( Ctr, SolveGrid( Ctr ) ) );
        return;
    }

    //! continuous monitoring, already breached at valuation : knock-in = vanilla,
    //! knock-out = 0.
    const double H = Ctr->Level();
    const double spot = Ctr->GetUnderlying()->GetSpot();
    if ( Ctr->IsUp() ? ( spot >= H ) : ( spot <= H ) )
    {
        GridResult res; //!< knocked out -> stays zero
        if ( Ctr->IsIn() )
        {
            InitGrid( Ctr, false );
            res = SolveGrid( Ctr ); //!< full vanilla
        }
        StoreResult( Ctr, res );
        return;
    }

    //! live : knock-out on the clamped (live-region) domain, knock-in by parity.
    InitGrid( Ctr, true );
    StoreResult( Ctr, KnockInParity( Ctr, SolveGrid( Ctr ) ) );
}

//! copy a grid solve's premium + spot Greeks into the contract's result entry.
void PricerPDE::StoreResult( Contract* Ctr, const GridResult& R )
{
    Result( Ctr ).premium = R.premium;
    Result( Ctr ).delta = R.delta;
    Result( Ctr ).gamma = R.gamma;
}

//! set up the grid for one contract: read the market (escrowed spot, ATM vol, carry
//! and discount rates, with the quanto correction), size the (s, t) domain from the
//! sigma factor, build the sinh coordinate change, set the boundary values (with
//! barrier handling), and precompute the escrowed-dividend PV and discrete-monitoring
//! step set. Called fresh before each SolveGrid (including the in/out-parity legs).
void PricerPDE::InitGrid( Contract* Ctr, bool ApplyBarrier )
{

    // cranck-nicholson scheme
    _theta = PDE_THETA;

    //
    _maturity = Ctr->GetMaturityDate();
    string Und = Ctr->GetUnderlying()->GetName();
    string Ccy = Ctr->GetPremiumCurrency()->GetName();

    // market data : carry r (drift, underlying ccy) and discount r_disc (premium
    // ccy). They coincide for a non-quanto contract (same currency).
    //! escrowed spot (== plain spot unless the underlying carries discrete
    //! dividends): nets the PV of dividends due before maturity, so the grid prices
    //! the same escrowed forward as the analytic and MCL engines.
    _s = Ctr->GetUnderlying()->GetDiffusionSpot( _maturity );
    _v = Ctr->GetUnderlying()->GetImplicitVol( 0, _maturity );
    //! carry = rate - continuous yield (dividend yield + repo), matching the ANA
    //! forward and the MCL drift node so the three engines agree
    _r = Ctr->GetUnderlying()->GetCurrency()->GetRate()->GetCurveValue( _maturity ) -
         Ctr->GetUnderlying()->DividendRepoYield( _maturity );
    _r_disc = Ctr->GetPremiumCurrency()->GetDiscountRate()->GetCurveValue( _maturity );

    //! quanto drift correction (payoff currency != underlying currency): the
    //! carry becomes r - rho(S,FX)*sigma_S*sigma_X while discounting stays in the
    //! premium currency. Matches the ANA quanto forward and the MCL node so the
    //! three engines agree. rho is the signed FX-underlying correlation; the
    //! sigma_S used is the ATM implied vol (exact under a flat BS surface, an
    //! approximation for local/stochastic vol). Currency identity is by pointer
    //! (Currency objects are singletons in the book graph), so this is exact.
    Currency* udl_ccy = Ctr->GetUnderlying()->GetCurrency();
    Currency* pre_ccy = Ctr->GetPremiumCurrency();
    double quanto_spread = 0; //!< maturity-average carry spread (grid centring / flat path)
    double quanto_v_fx = 0;   //!< sigma_FX, reused by the per-step forward spread below
    bool quanto_term = false; //!< term-structured rho: spread re-averaged per forward step
    if ( udl_ccy != pre_ccy )
    {
        if ( !_correlation )
        {
            ERR( "book pricing '" + _name + "': quanto contract '" + Ctr->GetName() +
                 "' requires a correlation matrix" );
        }
        quanto_v_fx = _correlation->GetFxVol( udl_ccy->GetName(), pre_ccy->GetName() );
        quanto_term = _correlation->IsTermStructured();
        //! rho over the whole horizon [0, T]: the running average (== the constant
        //! entry for a constant matrix), so the flat carry matches ANA's forward
        double rho = _correlation->GetTermValue( udl_ccy->GetName(), pre_ccy->GetName(), Und,
                                                 YearFraction( _today, _maturity ) );
        quanto_spread = rho * _v * quanto_v_fx;
        _r -= quanto_spread;
    }

    // original grid
    _t_max = YearFraction( _today, _maturity );
    _x_0_orig = _s;
    _x_max_orig = _s * exp( _pde->_custom_sigma_factor * _v * sqrt( _t_max ) );
    _x_min_orig = _s * exp( -_pde->_custom_sigma_factor * _v * sqrt( _t_max ) );
    _n = _pde->_custom_n_t;
    _j = _pde->_custom_n_s;
    _is_american = Ctr->IsAmerican();

    //! barrier handling (null for a non-barrier contract; flavour read off the type)
    Barrier* bar = dynamic_cast<Barrier*>( Ctr );
    if ( ApplyBarrier && bar )
    {
        bool is_up = bar->IsUp();
        double H = bar->Level();
        if ( bar->IsDiscrete() )
        {
            //! discrete: keep the full domain (the knocked region survives between
            //! monitoring dates) and hold the knocked-side boundary at zero.
            if ( is_up )
            {
                _v_up = 0;
                _v_dw = Ctr->Intrinsic( _x_min_orig );
            }
            else
            {
                _v_dw = 0;
                _v_up = Ctr->Intrinsic( _x_max_orig );
            }
        }
        else
        {
            //! continuous: move the live-side boundary onto the barrier and impose
            //! a zero Dirichlet condition there.
            if ( is_up )
            {
                _x_max_orig = H;
                _v_up = 0;
                _v_dw = Ctr->Intrinsic( _x_min_orig );
            }
            else
            {
                _x_min_orig = H;
                _v_dw = 0;
                _v_up = Ctr->Intrinsic( _x_max_orig );
            }
        }
    }
    else
    {
        _v_up = Ctr->Intrinsic( _x_max_orig );
        _v_dw = Ctr->Intrinsic( _x_min_orig );
    }

    // transformation settings -> more points around X_0
    //! _cc controls the sinh stretch strength (smaller -> denser clustering at X_0).
    //! Solving asinh on the domain ends fixes _aa/_bb so that x in [0,1] maps exactly
    //! onto X in [X_min, X_max] with X_0 sitting where the points are densest.
    _cc = .02;                                        //.02 / .05
    _c1 = asinh( ( _x_min_orig - _x_0_orig ) / _cc ); // particular case !!! [0,1] <-> [X_min, X_max]
    _c2 = asinh( ( _x_max_orig - _x_0_orig ) / _cc );
    _aa = _c2 - _c1;
    _bb = _c1;

    // transformed grid : uniform in x, so the spatial step _h and time step _k are constant
    _x_0 = Psi( _x_0_orig ); //!< spot in transformed coords (where we read the price)
    _x_min = Psi( _x_min_orig );
    _x_max = Psi( _x_max_orig );
    _h = ( _x_max - _x_min ) / (double)_j; //!< uniform space step (du)
    _k = _t_max / (double)_n;              //!< uniform time step (dt)
    _u_up = _v_up;
    _u_dw = _v_dw;

    //! operator inputs default to the flat maturity values (also what the
    //! variance-swap solve keeps for its documented flat-drift behaviour); the
    //! per-step vanilla/barrier path overwrites them before each assembly.
    _v_row = _v;
    _r_step = _r;
    _r_disc_step = _r_disc;

    //! per-step forward carry / discount rates off the full curves. Grid times
    //! t_i = i·k map to calendar dates (ACT/365), the last step pinned to the exact
    //! maturity, so the telescoped sum of step forwards reproduces the maturity
    //! zero·t_max exactly — the European price is unchanged by construction, while
    //! the American interim exercise now sees the true curve dynamics instead of
    //! the flat-r_T world. Flat curves leave every entry at _r / _r_disc and keep
    //! the assembled-once fast path.
    _fwd_carry.assign( _n, _r );
    _fwd_disc.assign( _n, _r_disc );
    _per_step_grid = false;
    {
        Underlying* und = Ctr->GetUnderlying();
        const YieldCurve* rate_udl = udl_ccy->GetRate();
        const YieldCurve* rate_disc = pre_ccy->GetDiscountRate();
        double z1 = 0, q1 = 0, zd1 = 0, t1 = 0; //!< t=0 terms vanish (z·t = 0)
        for ( int i = 0; i < _n; i++ )
        {
            const bool last = ( i == _n - 1 );
            const double t2 = last ? _t_max : ( i + 1 ) * _k;
            const date d2 = last ? _maturity
                                 : _today + boost::gregorian::days(
                                                lround( t2 * NB_OF_DAYS_A_YEAR ) );
            const double z2 = rate_udl->GetCurveValue( d2 );
            const double q2 = und->DividendRepoYield( d2 );
            const double zd2 = rate_disc->GetCurveValue( d2 );
            //! term-structured quanto: telescope int_0^t rho = rho_bar(t)*t exactly
            //! like the zero rates, so the summed per-step spreads reproduce the
            //! maturity integral; constant rho keeps the historic flat spread
            double quanto_step = quanto_spread;
            if ( quanto_term )
            {
                quanto_step =
                    ( _correlation->GetTermValue( udl_ccy->GetName(), pre_ccy->GetName(), Und, t2 ) * t2 -
                      _correlation->GetTermValue( udl_ccy->GetName(), pre_ccy->GetName(), Und, t1 ) * t1 ) /
                    _k * _v * quanto_v_fx;
            }
            _fwd_carry[i] = ( ( z2 - q2 ) * t2 - ( z1 - q1 ) * t1 ) / _k - quanto_step;
            _fwd_disc[i] = ( zd2 * t2 - zd1 * t1 ) / _k;
            if ( std::abs( _fwd_carry[i] - _r ) > 1e-12 || std::abs( _fwd_disc[i] - _r_disc ) > 1e-12 )
            {
                _per_step_grid = true; //!< sloped curve: re-assemble per step
            }
            z1 = z2;
            q1 = q2;
            zd1 = zd2;
            t1 = t2;
        }
    }

    //! local-vol mode: a mono underlying with a local surface (SABR) — the grid
    //! reads the Dupire local vol per node/step (like the MCL diffusion and the
    //! variance-swap source) instead of the single ATM vol; _v keeps sizing the
    //! domain, the wings and the quanto correction.
    _grid_single = nullptr;
    _local_vol = false;
    {
        SingleSet sset = Ctr->GetUnderlying()->GetSingleSet();
        if ( sset.size() == 1 && ( *sset.begin() )->GetVolatility()->_is_local )
        {
            _grid_single = *sset.begin();
            _local_vol = true;
        }
    }

    //! diffusion vol of the flat-vol grid: query the surface at the contract's
    //! strike when it has one. A moment-matched basket's equivalent vol is
    //! strike-dependent (the shifted-lognormal fit has skew) and the ANA engine
    //! prices at the strike's vol, so the grid must diffuse that same vol to
    //! agree away from the money. _v (the ATM query) keeps sizing the domain,
    //! the wings and the quanto correction — the documented ATM convention all
    //! three engines share; in local-vol mode the Dupire surface overrides per
    //! node, so the strike query is skipped.
    _v_diff = _v;
    if ( !_local_vol )
    {
        double k = 0;
        if ( auto* van = dynamic_cast<Vanilla*>( Ctr ) )
        {
            k = van->GetStrike();
        }
        else if ( auto* bar = dynamic_cast<Barrier*>( Ctr ) )
        {
            k = bar->_strike;
        }
        if ( k > 0 )
        {
            _v_diff = Ctr->GetUnderlying()->GetImplicitVol( k, _maturity );
        }
    }
    _v_row = _v_diff; //!< re-anchor the flat default (set above, before _v_diff existed)

    //! escrowed-dividend model: precompute the future-dividend PV at each grid time
    //! step so the American early-exercise test can recover the observed spot
    //! (escrowed value + PV) instead of testing on the escrowed value alone, which
    //! would over-value the option. Step i is at year-fraction i*k from today;
    //! map it to a calendar date (ACT/365) to read the curve. Left empty (no-op in
    //! ObservedSpot) when the underlying carries no discrete dividends.
    _future_pv.clear();
    Underlying* udl = Ctr->GetUnderlying();
    if ( _is_american && udl->FutureDividendPv( _today ) != 0 )
    {
        _future_pv.resize( _n + 1 );
        for ( int i = 0; i <= _n; i++ )
        {
            long days = lround( (double)i * _k * NB_OF_DAYS_A_YEAR );
            _future_pv[i] = udl->FutureDividendPv( _today + boost::gregorian::days( days ) );
        }
    }

    //! discrete monitoring : map each scheduled date to the nearest time step
    //! (step i is at year-fraction i*k from today). The knocked region is zeroed
    //! at these steps in SolveGrid.
    _discrete_monitor_steps.clear();
    if ( ApplyBarrier && bar && bar->IsDiscrete() )
    {
        _barrier_is_up = bar->IsUp();
        _barrier_level = bar->Level();
        for ( const date& d : Ctr->GetFixingDates() ) //!< == the monitoring schedule
        {
            int step = (int)lround( YearFraction( _today, d ) / _k );
            step = max( 0, min( _n, step ) );
            _discrete_monitor_steps.insert( step );
        }
    }

    //! autocallable: map each observation date to its time step and record the
    //! accrued rebate the layer is overwritten with above the autocall level.
    //! Steps clamp to [1, n-1]: the terminal redemption payoff already handles
    //! the >= level branch at n, and a step-0 overwrite would be DEAD (the
    //! premium is read off U_0, which the overwrite hook never touches), so a
    //! today-observation folds into the first step — one grid step late. Emplace
    //! keeps the EARLIER date's rebate should a very coarse grid collapse two
    //! observations onto one step (first trigger wins; the MCL keeps collapsed
    //! dates distinct, so a coarse grid diverges there by construction).
    _autocall_steps.clear();
    _phoenix_coupon = 0;
    _phoenix_coupon_level = 0;
    if ( auto* ac = dynamic_cast<Autocallable*>( Ctr ) )
    {
        _autocall_level = ac->AutocallLevel();
        if ( ac->IsPhoenix() )
        {
            _phoenix_coupon = ac->PeriodCoupon();
            _phoenix_coupon_level = ac->CouponLevel();
        }
        const vector<date>& obs = ac->GetAutocallDates();
        for ( size_t k = 0; k < obs.size(); k++ )
        {
            int step = (int)lround( YearFraction( _today, obs[k] ) / _k );
            step = max( 1, min( _n - 1, step ) );
            _autocall_steps.emplace( step, ac->Rebate( k + 1 ) );
        }
    }
}

//! zero the solved layer in the knocked region (called at each monitoring step)
void PricerPDE::ApplyDiscreteBarrier( la_vector* U )
{
    for ( int j = 0; j < _j + 1; j++ )
    {
        double X = Phi( j * _h );
        bool knocked = _barrier_is_up ? ( X >= _barrier_level ) : ( X <= _barrier_level );
        if ( knocked )
        {
            la_vector_set( U, j, 0 );
        }
    }
}

//! solve the 1-D transformed grid backward in time (Crank-Nicolson, theta = 1/2):
//! seed the terminal layer with the payoff, then march from maturity to today,
//! solving the tridiagonal system T_0 * U_0 = T_1 * U_1 at each step, projecting onto
//! the intrinsic for American exercise and zeroing the knocked region at discrete
//! barrier dates. Returns the premium and spot delta/gamma read off the solved layer.
PricerPDE::GridResult PricerPDE::SolveGrid( Contract* Ctr )
{

    // vectors & matrices allocations (RAII: freed on every exit, incl. ERR throw)
    LaVector diag_u = la_vector_calloc( _j );     // up diag
    LaVector diag_m = la_vector_calloc( _j + 1 ); // mid diag
    LaVector diag_d = la_vector_calloc( _j );     // down diag
    LaVector D_1 = la_vector_calloc( _j + 1 );    // D = T_1(j+1, j).u(j-1) + T_1(j, j).u(j) + T_1(j, j+1).u(j+1)
    LaVector U_0 = la_vector_calloc( _j + 1 );
    LaVector U_1 = la_vector_calloc( _j + 1 );
    // la_matrix * V        = la_matrix_calloc(J+1, N+1);

    //! the grid is uniform in x and the Black-Scholes operator coefficients depend only
    //! on x (not t), so the node spot levels Phi(j*h) are constant across the backward
    //! sweep: precompute them once and reuse for the payoff, the American exercise test
    //! and the discrete-barrier mask instead of re-evaluating sinh per step.
    vector<double> phi_node( _j + 1 );
    for ( int j = 0; j <= _j; j++ )
    {
        phi_node[j] = Phi( j * _h );
    }

    // init U_1 (boundaries)     u(x) = V(X)  X = phi(x) x =
    for ( int i = 0; i < _j + 1; i++ )
    {
        la_vector_set( U_1, i, Ctr->Intrinsic( phi_node[i] ) );
    }

    //! discrete barrier observed at maturity (terminal step N)
    if ( _discrete_monitor_steps.count( _n ) )
    {
        ApplyDiscreteBarrier( U_1 );
    }

    // la_matrix_set_col (V, N, U_1);
    // PrintList(U_1);

    // boundary rows of the implicit matrix T_0 are pinned to the identity
    // (Dirichlet boundaries) — fixed across every time step.
    la_vector_set( diag_u, 0, 0 );
    la_vector_set( diag_m, 0, 1 );
    la_vector_set( diag_m, _j, 1 );
    la_vector_set( diag_d, _j - 1, 0 );

    //! explicit RHS (T_1) interior diagonals, assembled together with T_0's below
    vector<double> t1_d( _j + 1, 0.0 ), t1_m( _j + 1, 0.0 ), t1_u( _j + 1, 0.0 );

    //! per-node vols for the step being assembled: the flat ATM vol, or the Dupire
    //! local vol refilled per step in _local_vol mode (like the varswap source)
    vector<double> sigma_node( _j + 1, _v_diff );

    //! flat curves + flat vol: the operator is time-independent, so the diagonals
    //! are assembled once (the fast path). A sloped curve or a local-vol surface
    //! makes them step-dependent and they are re-assembled per backward step.
    const bool per_step = _per_step_grid || _local_vol;

    //! assemble the interior T_0 / T_1 diagonals for one backward step: point the
    //! operator inputs (_r_step / _r_disc_step / _v_row) at this step's forward
    //! rates and node vols, then evaluate the three stencil entries per row (row j's
    //! entries all read the row-j coefficients a/b/c, so one _v_row per row is exact).
    auto assemble = [&]( int step )
    {
        if ( per_step )
        {
            _r_step = _fwd_carry[step];
            _r_disc_step = _fwd_disc[step];
            if ( _local_vol )
            {
                //! Dupire local vol at this step's calendar date (floored two days
                //! ahead of today so the FD stays regular, as the varswap solve does)
                long days_i = std::max( 2L, lround( (double)step * _k * NB_OF_DAYS_A_YEAR ) );
                date t_i = _today + boost::gregorian::days( days_i );
                for ( int j = 1; j < _j; j++ )
                {
                    sigma_node[j] = _grid_single->GetLocalVolatility( phi_node[j], t_i );
                }
            }
        }
        for ( int j = 1; j < _j; j++ )
        {
            _v_row = sigma_node[j];
            la_vector_set( diag_u, j, T_0( j, j + 1 ) );     //!< super-diagonal
            la_vector_set( diag_m, j, T_0( j, j ) );         //!< main diagonal
            la_vector_set( diag_d, j - 1, T_0( j, j - 1 ) ); //!< sub-diagonal
            t1_d[j] = T_1( j, j - 1 );                       //!< sub   (couples to U_1[j-1])
            t1_m[j] = T_1( j, j );                           //!< main  (couples to U_1[j])
            t1_u[j] = T_1( j, j + 1 );                       //!< super (couples to U_1[j+1])
        }
        _v_row = _v_diff; //!< restore the flat default for any caller outside the loop
    };

    //! autocallable: above the level the note redeems at the NEXT observation
    //! with certainty, so the top Dirichlet boundary is that rebate (or the
    //! terminal redemption after the last observation) DISCOUNTED to the current
    //! step — a constant terminal-payoff boundary would leak its undiscounted,
    //! full-coupon value into the interior across the whole sweep. The cumulative
    //! per-step discount sum makes the boundary follow the engine's own curve.
    //! The BOTTOM face keeps the constant linear-loss value N*x_min/S_ref: on the
    //! linear leg the discounting and the forward growth cancel exactly under
    //! flat rates, and the residual (the protection digital, ~0 at x_min) carries
    //! negligible weight — no time dependence needed there.
    vector<double> cum_disc;
    if ( !_autocall_steps.empty() )
    {
        cum_disc.assign( _n + 1, 0.0 );
        for ( int i = 1; i <= _n; i++ )
        {
            cum_disc[i] = cum_disc[i - 1] + _fwd_disc[i - 1] * _k;
        }
    }
    auto up_boundary = [&]( int i )
    {
        if ( _autocall_steps.empty() )
        {
            return _u_up; //!< vanilla / barrier: the historic constant boundary
        }
        int m = _n;
        double val = _u_up; //!< after the last observation: the terminal redemption
        if ( auto next = _autocall_steps.upper_bound( i ); next != _autocall_steps.end() )
        {
            m = next->first;
            val = next->second;
        }
        return val * exp( -( cum_disc[m] - cum_disc[i] ) );
    };

    //! Rannacher restart for the autocall overwrites: the overwrite drops a large
    //! discontinuity (rebate vs continuation) into the layer, and Crank-Nicolson
    //! (theta = 1/2) propagates such data with slowly-decaying oscillations that
    //! poison the price by O(1) amounts. The standard cure: run the next few
    //! backward steps FULLY IMPLICIT (unconditionally smoothing), then return to
    //! CN. NOTE the engine's convention T_0 = I + k(1-theta)L puts the implicit
    //! weight on (1 - theta), so fully implicit is theta = 0 here. The theta
    //! switch forces an operator re-assembly on the flat fast path. (The
    //! discrete-barrier zeroing needs none of this: an up-and-out value already
    //! vanishes continuously at its barrier.)
    constexpr int RANNACHER_STEPS = 2;
    int rannacher_left = 0;

    // backwarding
    for ( int i = _n - 1; i >= 0; i-- )
    {
        //! theta for THIS solve: fully implicit (theta = 0 in this engine's
        //! convention) while a Rannacher restart is active, Crank-Nicolson otherwise
        bool theta_switch = false;
        const double want_theta = ( rannacher_left > 0 ) ? 0.0 : PDE_THETA;
        if ( _theta != want_theta )
        {
            _theta = want_theta;
            theta_switch = true;
        }
        if ( rannacher_left > 0 )
        {
            rannacher_left--;
        }

        //! step coefficients: once on the first iteration for the time-independent
        //! fast path, every step when the curve or the vol is time-dependent —
        //! and whenever the Rannacher theta just switched
        if ( per_step || i == _n - 1 || theta_switch )
        {
            assemble( i );
        }

        // right side
        la_vector_set( D_1, 0, _u_dw );             // down boundary (fonction de t?)
        la_vector_set( D_1, _j, up_boundary( i ) ); // up boundary: discounted next rebate for an autocall
        for ( int j = 1; j < _j; j++ )
        {
            la_vector_set( D_1, j,
                           t1_d[j] * la_vector_get( U_1, j - 1 ) +
                               t1_m[j] * la_vector_get( U_1, j ) +
                               t1_u[j] * la_vector_get( U_1, j + 1 ) );
        }

        // PrintList(D_1);

        // solving system
        SolveTridiagonal( diag_m, diag_u, diag_d, D_1, U_0 );

        // american mode : max(intrinsec value, expected value). The grid value
        // Phi(k*h) is the escrowed spot; exercise is against the OBSERVED spot
        // (escrowed + future-dividend PV at this step i), matching the MCL engine.
        if ( _is_american )
        {
            for ( int k = 0; k < _j + 1; k++ )
            {
                U_0->data[k] = max( U_0->data[k], Ctr->Intrinsic( ObservedSpot( phi_node[k], i ) ) );
            }
        }

        // copy U_0 to U_1 for next step
        la_vector_memcpy( U_1, U_0 );

        //! discrete barrier: zero the knocked region at scheduled steps
        if ( _discrete_monitor_steps.count( i ) )
        {
            ApplyDiscreteBarrier( U_1 );
        }

        //! autocall observation: the note redeems at/above the level, so the
        //! continuation value there IS the rebate (paid at this step); a Phoenix
        //! additionally detaches its period coupon in the [coupon level, autocall
        //! level) zone — the coupon rides ON TOP of the continuation. Either
        //! discontinuity triggers a Rannacher restart.
        if ( auto it = _autocall_steps.find( i ); it != _autocall_steps.end() )
        {
            for ( int j = 0; j < _j + 1; j++ )
            {
                if ( phi_node[j] >= _autocall_level )
                {
                    la_vector_set( U_1, j, it->second );
                }
                else if ( _phoenix_coupon > 0 && phi_node[j] >= _phoenix_coupon_level )
                {
                    la_vector_set( U_1, j, la_vector_get( U_1, j ) + _phoenix_coupon );
                }
            }
            rannacher_left = RANNACHER_STEPS;
        }
        // la_matrix_set_col (V, i, U_0);
    }

    // read pricer from grid
    // PrintList(U_0);
    // PrintMatrix(V);

    // read premium & spot greeks off the solved grid
    GridResult result;

    // premium, gamma
    if ( _request_premium || _request_gamma )
    {
        result.premium = GetGridPrice( _x_0, U_0 );
    }

    // delta, gamma : central differences around X_0 with spot half-bump h = X_0 * SHIFT/2
    const double h = _x_0_orig * GREEK_SPOT_BUMP / 2;
    double delta_price_sup, delta_price_inf;
    if ( _request_delta || _request_gamma )
    {
        double x_inf = Psi( _x_0_orig - h );
        double x_sup = Psi( _x_0_orig + h );
        delta_price_inf = GetGridPrice( x_inf, U_0 );
        delta_price_sup = GetGridPrice( x_sup, U_0 );
        result.delta = ( delta_price_sup - delta_price_inf ) / ( 2 * h ); //!< dV/dS
    }

    if ( _request_gamma )
    {
        result.gamma = ( delta_price_sup + delta_price_inf - 2 * result.premium ) / ( h * h ); //!< d2V/dS2
    }

    return result; //!< LaVector members above free themselves at scope exit
}

//! Variance swap on the 1-D spot grid. Solves the expected accumulated variance
//!   u_t + 0.5 sigma^2 S^2 u_SS + (r-q) S u_S + sigma^2 = 0,  u(.,T) = 0
//! (Kolmogorov backward, NO discount — it is an undiscounted expectation — with a
//! +sigma^2 source). The fair annualized variance is u(S0,0)/T, and the value is
//!   PV = notional * DF * (fair_var - strike_var).
//! sigma^2 here is the Dupire LOCAL variance sigma_loc(S, t)^2 read per grid node,
//! so the grid integrates the same implied smile the static-replication analytic
//! does and the two agree (for a flat surface the local vol equals the flat vol, so
//! it still reproduces sigma^2 exactly). The far-wing Dirichlet boundaries use the
//! flat ATM variance (negligible probability weight there).
//! Reuses the transformed grid + Crank-Nicolson solve, with C() = 0 (_variance_mode).
void PricerPDE::SolveVarianceSwap( VarianceSwap* Ctr )
{
    InitGrid( Ctr, false ); //!< transformed grid, v (ATM vol), r, T_max, ...
    _variance_mode = true;  //!< drop the reaction term: C() = 0

    const double atm_var = _v * _v; //!< ATM variance: far-wing boundaries + fallback

    //! the Dupire surface lives on a single name; a multi-name underlying has none,
    //! so fall back to the flat ATM variance there. Precompute each node's spot
    //! level Phi(j*h) once (fixed across the backward time steps).
    SingleSet sset = Ctr->GetUnderlying()->GetSingleSet();
    Single* single = ( sset.size() == 1 ) ? *sset.begin() : nullptr;
    vector<double> spot( _j + 1 );
    for ( int j = 0; j <= _j; j++ )
    {
        spot[j] = Phi( j * _h );
    }
    vector<double> src( _j + 1, atm_var ); //!< per-step local-variance source, reused

    LaVector diag_u = la_vector_calloc( _j );
    LaVector diag_m = la_vector_calloc( _j + 1 );
    LaVector diag_d = la_vector_calloc( _j );
    LaVector D_1 = la_vector_calloc( _j + 1 );
    LaVector U_0 = la_vector_calloc( _j + 1 );
    LaVector U_1 = la_vector_calloc( _j + 1 );

    //! terminal condition: zero accumulated variance at maturity (U_1 starts at 0)

    //! diagonals (c = 0 in variance mode)
    la_vector_set( diag_u, 0, 0 );
    la_vector_set( diag_m, 0, 1 );
    la_vector_set( diag_m, _j, 1 );
    la_vector_set( diag_d, _j - 1, 0 );
    for ( int j = 1; j < _j; j++ )
    {
        la_vector_set( diag_u, j, T_0( j, j + 1 ) );
        la_vector_set( diag_m, j, T_0( j, j ) );
        la_vector_set( diag_d, j - 1, T_0( j, j - 1 ) );
    }

    for ( int i = _n - 1; i >= 0; i-- )
    {
        const double tau = _t_max - i * _k; //!< remaining time -> remaining variance

        //! instantaneous-variance source at this step's calendar date: the Dupire
        //! local variance per node sigma_loc(S_j, t_i)^2. The evaluation date is
        //! floored two days ahead of today so the local-vol finite differences stay
        //! regular (T > 0) on the first steps; for a flat surface src stays atm_var.
        if ( single )
        {
            long days_i = std::max( 2L, lround( (double)i * _k * NB_OF_DAYS_A_YEAR ) );
            date t_i = _today + boost::gregorian::days( days_i );
            for ( int j = 1; j < _j; j++ )
            {
                double lv = single->GetLocalVolatility( spot[j], t_i );
                src[j] = lv * lv;
            }
        }

        la_vector_set( D_1, 0, atm_var * tau ); //!< Dirichlet far-wing boundaries
        la_vector_set( D_1, _j, atm_var * tau );
        for ( int j = 1; j < _j; j++ )
        {
            double rhs = T_1( j, j - 1 ) * la_vector_get( U_1, j - 1 ) +
                         T_1( j, j ) * la_vector_get( U_1, j ) +
                         T_1( j, j + 1 ) * la_vector_get( U_1, j + 1 );
            la_vector_set( D_1, j, rhs + _k * src[j] ); //!< + source sigma_loc^2 dt
        }
        SolveTridiagonal( diag_m, diag_u, diag_d, D_1, U_0 );
        la_vector_memcpy( U_1, U_0 );
    }

    double fair_var = ( _t_max > 0 ) ? GetGridPrice( _x_0, U_0 ) / _t_max : atm_var;
    _variance_mode = false;

    //! discrete observation: the grid solves the continuous expected accumulated
    //! variance; a discrete fixing schedule adds the same deterministic drift^2
    //! term the ANA strip adds (see VarianceSwap::ObservationDriftVariance)
    fair_var += Ctr->ObservationDriftVariance( _today );

    const double k_var = Ctr->GetVolatilityStrike() * Ctr->GetVolatilityStrike();
    const double df = Ctr->GetPremiumCurrency()->GetDiscountRate()->GetDiscountFactor( _maturity );

    //! seasoned swap: time-weight the realised past leg (from the fixings) with
    //! the grid's future fair variance — fair_total = (past + fair*T_fut)/T_total;
    //! the last-fixing -> spot bridge gives the analytic delta/gamma (matching the
    //! ANA treatment, so the two engines report the same Greeks)
    double delta = 0, gamma = 0;
    if ( Ctr->IsSeasoned() )
    {
        const double t_fut = YearFraction( _today, _maturity );
        const double t_tot = Ctr->GetTotalYearFraction();
        fair_var = ( Ctr->PastSumSquaredReturns() + fair_var * t_fut ) / t_tot;
        const double s = Ctr->GetUnderlying()->GetSpot();
        const double scale = Ctr->GetNotional() * df / t_tot;
        const double log_bridge = Ctr->LastFixingLogBridge();
        delta = scale * 2 * log_bridge / s;
        gamma = scale * 2 * ( 1 - log_bridge ) / ( s * s );
    }

    Result( Ctr ).premium = Ctr->GetNotional() * df * ( fair_var - k_var );
    Result( Ctr ).delta = delta;
    Result( Ctr ).gamma = gamma;
}

//! coordinate change X = Phi(x): a sinh stretch that clusters grid points around
//! the spot X_0 (where accuracy matters most) while still reaching the far wings
inline double PricerPDE::Phi( double x )
{
    return _x_0_orig + _cc * sinh( _aa * x + _bb );
}
//! first derivative dX/dx of the stretch (the Jacobian used to transform the PDE)
inline double PricerPDE::Phi_x( double x )
{
    return _cc * _aa * cosh( _aa * x + _bb );
}
//! second derivative d2X/dx2 of the stretch (enters the transformed drift b(x))
inline double PricerPDE::Phi_xx( double x )
{
    return _cc * _aa * _aa * sinh( _aa * x + _bb );
}
//! inverse map x = Psi(X): from a spot level back to the uniform x grid coordinate
inline double PricerPDE::Psi( double X )
{
    return ( asinh( ( X - _x_0_orig ) / _cc ) - _bb ) / _aa;
}
//! diffusion coefficient of the Black-Scholes operator in X: 0.5 sigma^2 S^2 (the
//! sign is carried so V_t = A V_XX + B V_X + C V matches the backward convention).
//! _v_row is the vol at the row being assembled: the ATM vol, or the Dupire local
//! vol of the node in _local_vol mode.
double PricerPDE::A( double x )
{
    return -.5 * _v_row * _v_row * x * x;
}
//! drift coefficient: -r S (the current step's forward carry, underlying currency;
//! equals the maturity zero on a flat curve)
double PricerPDE::B( double x )
{
    return -_r_step * x;
}
//! reaction (discount) coefficient: the current step's forward discount rate
//! (premium currency; the maturity zero on a flat curve)
double PricerPDE::C( double /*x*/ )
{
    //! variance-swap solve accumulates expected variance (an undiscounted
    //! expectation), so there is no reaction/discount term there.
    return _variance_mode ? 0.0 : _r_disc_step;
}
//! transformed diffusion a(x) = A(Phi(x)) / Phi_x^2 (chain rule on V_XX -> u_xx)
inline double PricerPDE::a( double x )
{
    return A( Phi( x ) ) / Phi_x( x ) / Phi_x( x );
}
//! transformed drift b(x): the chain rule mixes A's curvature into the drift via
//! Phi_xx, hence the second term
inline double PricerPDE::b( double x )
{
    return B( Phi( x ) ) / Phi_x( x ) - A( Phi( x ) ) * Phi_xx( x ) / Phi_x( x ) / Phi_x( x ) / Phi_x( x );
}
//! transformed reaction c(x) = C(Phi(x)) (the discount is coordinate-independent)
inline double PricerPDE::c( double x )
{
    return C( Phi( x ) );
}
//! spatial operator L as a tridiagonal stencil: only the three diagonals are
//! non-zero (i==j, i==j-1, i==j+1); everything else is 0
inline double PricerPDE::L( int i, int j )
{
    if ( i == j )
    {
        return L_m( i );
    }
    else if ( i == j - 1 )
    {
        return L_u( i );
    }
    else if ( i == j + 1 )
    {
        return L_d( i );
    }
    else
    {
        return 0;
    }
}
//! super-diagonal (coupling to u(j+1)): central drift b/2h + diffusion a/h^2
inline double PricerPDE::L_u( int j )
{
    return b( j * _h ) / 2 / _h + a( j * _h ) / _h / _h;
}
//! main diagonal (coupling to u(j)): reaction c minus the diffusion's -2a/h^2
inline double PricerPDE::L_m( int j )
{
    return c( j * _h ) - 2 * a( j * _h ) / _h / _h;
}
//! sub-diagonal (coupling to u(j-1)): -drift b/2h + diffusion a/h^2
inline double PricerPDE::L_d( int j )
{
    return -b( j * _h ) / 2 / _h + a( j * _h ) / _h / _h;
}
//! identity stencil (Kronecker delta) used to assemble the Crank-Nicolson matrices
inline double PricerPDE::I( int i, int j )
{
    return ( i == j ) ? 1 : 0;
}
//! left-hand (implicit) matrix T_0 = I + k(1-theta)L applied to the new layer U_0
inline double PricerPDE::T_0( int i, int j )
{
    return I( i, j ) + _k * ( 1 - _theta ) * L( i, j );
}
//! right-hand (explicit) matrix T_1 = I - k*theta*L applied to the known layer U_1
inline double PricerPDE::T_1( int i, int j )
{
    return I( i, j ) - _k * _theta * L( i, j );
}
//! ----------------------------------------------------------------------
//! Heston 2-D (S, v) finite-difference pricer (Douglas ADI)
//! ----------------------------------------------------------------------

//! true iff the contract's underlying is a mono with a stochastic-vol (Heston)
bool PricerPDE::UnderlyingIsHeston( Contract* Ctr )
{
    SingleSet s = Ctr->GetUnderlying()->GetSingleSet();
    if ( s.size() != 1 )
    {
        return false;
    }
    return ( *s.begin() )->GetVolatility()->IsStochastic();
}

namespace
{
//! solve a tridiagonal system (sub a[1..n-1], diag d[0..n-1], super c[0..n-2])
//! in place using the Thomas algorithm; rhs/solution in x.
void Thomas( vector<double>& a, vector<double>& d, vector<double>& c, vector<double>& x )
{
    const int n = (int)d.size();
    for ( int i = 1; i < n; i++ )
    {
        double w = a[i] / d[i - 1];
        d[i] -= w * c[i - 1];
        x[i] -= w * x[i - 1];
    }
    x[n - 1] /= d[n - 1];
    for ( int i = n - 2; i >= 0; i-- )
    {
        x[i] = ( x[i] - c[i] * x[i + 1] ) / d[i];
    }
}
} // namespace

//! Douglas ADI for the Heston (or LSV) PDE
//!   V_t + 0.5 L^2 v S^2 V_SS + rho xi L v S V_Sv + 0.5 xi^2 v V_vv
//!        + b S V_S + kappa(theta - v) V_v - r_d V = 0
//! with b the carry (from the forward), r_d the premium-currency discount and
//! L(S,t) the LSV leverage (identically 1 for pure Heston; calibrated against
//! the target surface via Single::CalibrateLeverage for an LSV vol). Cross term
//! explicit; S- and v-sweeps implicit (theta = 1/2). European or, if requested,
//! American by intrinsic projection after each step.
PricerPDE::GridResult PricerPDE::SolveHestonGrid( Contract* Ctr )
{
    Single* single = *Ctr->GetUnderlying()->GetSingleSet().begin();
    const StochasticVolParams hv = single->GetVolatility()->StochasticParams();
    const bool lsv = single->GetVolatility()->IsLsv();

    const date mat = Ctr->GetMaturityDate();
    const double T = YearFraction( _today, mat );
    const double S0 = single->GetSpot();
    const double F = Ctr->GetUnderlying()->GetForward( mat, Ctr->GetPremiumCurrency() );
    const double rd = Ctr->GetPremiumCurrency()->GetDiscountRate()->GetCurveValue( mat );
    const double b = ( T > 0 ) ? log( F / S0 ) / T : 0.0;
    const double v0 = hv.v0;
    const double kap = hv.kappa;
    const double th = hv.theta;
    const double xi = hv.xi;
    const double rho = hv.rho;
    const bool american = Ctr->IsAmerican();

    //! Bates jumps (lambda = 0 -> pure Heston): lognormal jump size e^Z, Z~N(muJ,
    //! sigJ^2). The PIDE adds an explicit jump integral lambda*E_J[V(S e^Z) - V] and
    //! a -lambda*kbar*S V_S compensator drift (folded into the diffusion drift
    //! bdrift, kept implicit).
    const double lambda = hv.jump_intensity;
    const double muJ = hv.jump_mean;
    const double sigJ = hv.jump_vol;
    const bool bates = lambda > 0.0;

    //! Jump-size quadrature: discretise Z ~ N(muJ, sigJ^2) on a trapezoid grid out
    //! to +/-span sigma, re-normalised for truncation (sum w = 1). The compensator
    //! MUST use the same discretised mean jump as this quadrature, not the analytic
    //! kbar = e^{muJ+0.5 sigJ^2}-1: with the renormalised weights the discretised
    //! mean differs slightly, and if the -lambda*kbar*S V_S drift does not match the
    //! mean of the explicit jump integral the two fail to cancel on the forward, so
    //! the scheme carries a price bias that grows with the jump vol. Hence
    //! kbar = sum_m w_m (e^{z_m} - 1), consistent with the integral applied below.
    const int NJ = BATES_JUMP_QUAD_POINTS;
    vector<double> zval, zwt;
    double kbar = 0.0;
    if ( bates )
    {
        const double sg = std::max( sigJ, 1e-8 );
        const double zlo = muJ - BATES_JUMP_SIGMA_SPAN * sg, zhi = muJ + BATES_JUMP_SIGMA_SPAN * sg, dz = ( zhi - zlo ) / NJ;
        double wsum = 0;
        for ( int m = 0; m <= NJ; m++ )
        {
            double z = zlo + m * dz;
            double pdf = exp( -0.5 * ( z - muJ ) * ( z - muJ ) / ( sg * sg ) ) / ( sg * sqrt( 2 * M_PI ) );
            double w = pdf * dz * ( ( m == 0 || m == NJ ) ? 0.5 : 1.0 ); //!< trapezoid
            zval.push_back( z );
            zwt.push_back( w );
            wsum += w;
        }
        for ( double& w : zwt )
            w /= wsum; //!< normalise to sum 1 (truncation correction)
        for ( int m = 0; m <= NJ; m++ )
            kbar += zwt[m] * ( exp( zval[m] ) - 1.0 ); //!< discretised mean jump (matches the integral)
    }
    const double bdrift = b - lambda * kbar; //!< == b for pure Heston

    //! grid extents. For LSV the effective spot variance is L^2 v, which tracks the
    //! target surface — widen the S-domain with the target ATM variance so a
    //! high-vol target on a low-v0 Heston base still fits the grid.
    double vgrid = std::max( v0, th );
    if ( lsv )
    {
        const double atm = single->GetImplicitVol( 0, mat );
        vgrid = std::max( vgrid, atm * atm );
    }
    const double vmax = std::max( 1.0, HESTON_VMAX_FACTOR * std::max( v0, th ) );
    const double smax = std::max( HESTON_SMAX_MIN_FACTOR * S0, S0 * exp( HESTON_SMAX_SIGMA_FACTOR * sqrt( vgrid * std::max( T, 1.0 ) ) ) );
    //! grid resolution from the book's vanilla_precision (see the constants above)
    int NS = HESTON_GRID_NS_MEDIUM, Nv = HESTON_GRID_NV_MEDIUM, Nt = HESTON_GRID_NT_MEDIUM;
    switch ( _pde->_vanilla_precision )
    {
    case Precision::Low:
        NS = HESTON_GRID_NS_LOW, Nv = HESTON_GRID_NV_LOW, Nt = HESTON_GRID_NT_LOW;
        break;
    case Precision::Medium:
        NS = HESTON_GRID_NS_MEDIUM, Nv = HESTON_GRID_NV_MEDIUM, Nt = HESTON_GRID_NT_MEDIUM;
        break;
    case Precision::High:
        NS = HESTON_GRID_NS_HIGH, Nv = HESTON_GRID_NV_HIGH, Nt = HESTON_GRID_NT_HIGH;
        break;
    }
    const double dS = smax / NS;
    const double dv = vmax / Nv;
    const double dt = T / Nt;
    const double q = 0.5; //!< ADI implicitness (Crank-Nicolson directional)

    //! LSV leverage: calibrate on a (day-granular) uniform date grid to maturity;
    //! lev_col holds the per-S-column leverage of the current time step, refilled
    //! once per step so the explicit predictor and the implicit corrector apply
    //! the SAME S-operator (a Douglas-ADI consistency requirement). Identically 1
    //! for pure Heston.
    LeverageSurface lev;
    if ( lsv )
    {
        const long total_days = ( mat - _today ).days();
        const int ncal = (int)std::max<long>( 1, std::min<long>( Nt, total_days ) );
        vector<date> cal_dates{ _today };
        for ( int n = 1; n <= ncal; n++ )
        {
            const date d = _today + days( ( total_days * n ) / ncal );
            if ( d > cal_dates.back() )
            {
                cal_dates.push_back( d );
            }
        }
        lev = single->CalibrateLeverage( cal_dates );
    }
    vector<double> lev_col( NS + 1, 1.0 );

    auto S = [&]( int i )
    { return i * dS; };
    auto V = [&]( int i )
    { return i * dv; };
    auto IX = [&]( int i, int j )
    { return i * ( Nv + 1 ) + j; };

    vector<double> U( ( NS + 1 ) * ( Nv + 1 ), 0.0 );

    //! terminal payoff
    for ( int i = 0; i <= NS; i++ )
    {
        double payoff = Ctr->Intrinsic( S( i ) );
        for ( int j = 0; j <= Nv; j++ )
        {
            U[IX( i, j )] = payoff;
        }
    }
    const double intrinsic0 = Ctr->Intrinsic( 0.0 ); //!< payoff at S=0 (0 call, K put)

    //! Bates jump integral: read V at the jumped spot S*e^Z by linear interpolation
    //! in S (linear extrapolation past Smax), weighted by the zval/zwt quadrature
    //! built above (the same one whose discretised mean feeds the kbar compensator).
    auto Vinterp = [&]( double St, int j, const vector<double>& g )
    {
        double fi = St / dS; //!< grid index (S(i) = i*dS)
        if ( fi <= 0 )
            return g[IX( 0, j )];
        int i = (int)fi;
        if ( i >= NS ) //!< linear extrapolation beyond Smax
            return g[IX( NS, j )] + ( fi - NS ) * ( g[IX( NS, j )] - g[IX( NS - 1, j )] );
        double w = fi - i;
        return ( 1 - w ) * g[IX( i, j )] + w * g[IX( i + 1, j )];
    };
    auto Jump = [&]( int i, int j, const vector<double>& g ) //!< lambda*(E_J[V(S e^Z)] - V)
    {
        double ej = 0;
        for ( int m = 0; m <= NJ; m++ )
            ej += zwt[m] * Vinterp( S( i ) * exp( zval[m] ), j, g );
        return lambda * ( ej - g[IX( i, j )] );
    };

    //! per-step operator applications and tridiagonal coefficients
    auto A1 = [&]( int i, int j, const vector<double>& g ) //!< S-direction (+ half discount)
    {
        double si = S( i ), vj = V( j );
        double lij = lev_col[i]; //!< LSV leverage of this S-column (1 for pure Heston)
        double vss = ( g[IX( i + 1, j )] - 2 * g[IX( i, j )] + g[IX( i - 1, j )] ) / ( dS * dS );
        double vs = ( g[IX( i + 1, j )] - g[IX( i - 1, j )] ) / ( 2 * dS );
        return 0.5 * lij * lij * vj * si * si * vss + bdrift * si * vs - 0.5 * rd * g[IX( i, j )];
    };
    auto A2 = [&]( int i, int j, const vector<double>& g ) //!< v-direction (+ half discount)
    {
        double vj = V( j );
        if ( j == 0 ) //!< v=0 : no diffusion, forward (upwind) drift kappa*theta>0
        {
            double vv = ( g[IX( i, 1 )] - g[IX( i, 0 )] ) / dv;
            return kap * th * vv - 0.5 * rd * g[IX( i, 0 )];
        }
        double vvv = ( g[IX( i, j + 1 )] - 2 * g[IX( i, j )] + g[IX( i, j - 1 )] ) / ( dv * dv );
        double vv = ( g[IX( i, j + 1 )] - g[IX( i, j - 1 )] ) / ( 2 * dv );
        return 0.5 * xi * xi * vj * vvv + kap * ( th - vj ) * vv - 0.5 * rd * g[IX( i, j )];
    };
    auto A0 = [&]( int i, int j, const vector<double>& g ) //!< cross term (explicit)
    {
        if ( j == 0 )
        {
            return 0.0;
        }
        double vsv = ( g[IX( i + 1, j + 1 )] - g[IX( i + 1, j - 1 )] - g[IX( i - 1, j + 1 )] + g[IX( i - 1, j - 1 )] ) /
                     ( 4 * dS * dv );
        //! d<S,v> = rho xi (L sqrt(v)) sqrt(v) S dt: the leverage scales the cross term linearly
        return rho * xi * lev_col[i] * V( j ) * S( i ) * vsv;
    };

    auto fill_boundaries = [&]( vector<double>& g, double tau )
    {
        for ( int j = 0; j <= Nv; j++ )
        {
            g[IX( 0, j )] = intrinsic0 * exp( -rd * tau );                //!< S=0 (Dirichlet)
            g[IX( NS, j )] = 2 * g[IX( NS - 1, j )] - g[IX( NS - 2, j )]; //!< S=Smax (linear)
        }
        for ( int i = 0; i <= NS; i++ )
        {
            g[IX( i, Nv )] = g[IX( i, Nv - 1 )]; //!< v=Vmax (Neumann)
        }
    };

    vector<double> Y0 = U, Y1 = U;
    //! Thomas-solver scratch, reused across every sweep of every time step (resized
    //! per sweep to NS-1 or Nv; capacity stabilises after the first step, so no
    //! per-iteration allocation inside the Nt loop)
    vector<double> a, d, c, x;
    for ( int n = 1; n <= Nt; n++ )
    {
        const double tau = n * dt;
        //! LSV: the leverage of this step's calendar time (tau is time since
        //! maturity — the grid marches backward), one value per S-column
        if ( lsv )
        {
            const double t_cal = std::max( 0.0, T - tau );
            for ( int i = 0; i <= NS; i++ )
            {
                lev_col[i] = lev.GetLeverage( S( i ), t_cal );
            }
        }
        fill_boundaries( U, ( n - 1 ) * dt );

        //! explicit full-operator step Y0 = U + dt*(A0+A1+A2)U
        for ( int i = 1; i < NS; i++ )
        {
            for ( int j = 0; j < Nv; j++ )
            {
                Y0[IX( i, j )] = U[IX( i, j )] + dt * ( A0( i, j, U ) + A1( i, j, U ) + A2( i, j, U ) +
                                                        ( bates ? Jump( i, j, U ) : 0.0 ) );
            }
        }

        //! implicit S-sweep : (I - q dt A1) Y1 = Y0 - q dt A1 U, for each j
        for ( int j = 0; j < Nv; j++ )
        {
            int m = NS - 1; //!< unknowns i = 1..NS-1
            a.resize( m );
            d.resize( m );
            c.resize( m );
            x.resize( m );
            double vj = V( j );
            for ( int i = 1; i < NS; i++ )
            {
                double si = S( i );
                double lij = lev_col[i]; //!< must match the A1 operator above
                double diff = 0.5 * lij * lij * vj * si * si / ( dS * dS );
                double drift = bdrift * si / ( 2 * dS );
                double lo = diff - drift, up = diff + drift, di = -2 * diff - 0.5 * rd;
                int idx = i - 1;
                a[idx] = -q * dt * lo;
                d[idx] = 1 - q * dt * di;
                c[idx] = -q * dt * up;
                x[idx] = Y0[IX( i, j )] - q * dt * A1( i, j, U );
                if ( i == 1 )
                {
                    x[idx] -= a[idx] * U[IX( 0, j )]; //!< known S=0 boundary
                    a[idx] = 0;
                }
                if ( i == NS - 1 )
                {
                    x[idx] -= c[idx] * U[IX( NS, j )]; //!< lagged S=Smax boundary
                    c[idx] = 0;
                }
            }
            Thomas( a, d, c, x );
            for ( int i = 1; i < NS; i++ )
            {
                Y1[IX( i, j )] = x[i - 1];
            }
        }

        //! implicit v-sweep : (I - q dt A2) U = Y1 - q dt A2 U, for each i
        for ( int i = 1; i < NS; i++ )
        {
            int m = Nv; //!< unknowns j = 0..Nv-1
            a.resize( m );
            d.resize( m );
            c.resize( m );
            x.resize( m );
            for ( int j = 0; j < Nv; j++ )
            {
                double vj = V( j );
                double lo, di, up;
                if ( j == 0 )
                {
                    lo = 0;
                    up = kap * th / dv;
                    di = -kap * th / dv - 0.5 * rd;
                }
                else
                {
                    double diff = 0.5 * xi * xi * vj / ( dv * dv );
                    double drift = kap * ( th - vj ) / ( 2 * dv );
                    lo = diff - drift;
                    up = diff + drift;
                    di = -2 * diff - 0.5 * rd;
                }
                a[j] = -q * dt * lo;
                d[j] = 1 - q * dt * di;
                c[j] = -q * dt * up;
                x[j] = Y1[IX( i, j )] - q * dt * A2( i, j, U );
                if ( j == Nv - 1 )
                {
                    x[j] -= c[j] * U[IX( i, Nv )]; //!< lagged v=Vmax boundary
                    c[j] = 0;
                }
            }
            a[0] = 0;
            Thomas( a, d, c, x );
            for ( int j = 0; j < Nv; j++ )
            {
                U[IX( i, j )] = x[j];
            }
        }

        fill_boundaries( U, tau );

        //! American : early-exercise projection
        if ( american )
        {
            for ( int i = 0; i <= NS; i++ )
            {
                double ev = Ctr->Intrinsic( S( i ) );
                for ( int j = 0; j <= Nv; j++ )
                {
                    U[IX( i, j )] = std::max( U[IX( i, j )], ev );
                }
            }
        }
    }

    int j0 = std::min( Nv - 1, std::max( 0, (int)( v0 / dv ) ) );
    double wv = ( v0 - V( j0 ) ) / dv;
    auto at = [&]( int i, int j )
    { return U[IX( i, j )]; };

    //! bilinear price at an arbitrary spot St (v-interpolated at v0, S-interpolated)
    auto price_at_spot = [&]( double St )
    {
        double fi = std::min( (double)( NS - 1 ), std::max( 0.0, St / dS ) );
        int i = (int)fi;
        double ws = fi - i;
        double pi = ( 1 - wv ) * at( i, j0 ) + wv * at( i, j0 + 1 );
        double pi1 = ( 1 - wv ) * at( i + 1, j0 ) + wv * at( i + 1, j0 + 1 );
        return ( 1 - ws ) * pi + ws * pi1;
    };

    //! delta / gamma by a central spot bump of GREEK_SPOT_BUMP (consistent with the
    //! 1-D PDE and the bump-and-revalue baseline), not the coarse raw grid step dS.
    const double hb = S0 * GREEK_SPOT_BUMP / 2;
    GridResult res;
    res.premium = price_at_spot( S0 );
    res.delta = ( price_at_spot( S0 + hb ) - price_at_spot( S0 - hb ) ) / ( 2 * hb );
    res.gamma = ( price_at_spot( S0 + hb ) - 2 * res.premium + price_at_spot( S0 - hb ) ) / ( hb * hb );
    return res;
}
