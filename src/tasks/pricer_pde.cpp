#include "thoth.hpp"
#include "pricer_pde.hpp"
#include "barrier.hpp"
#include "cancellation.hpp"
#include "object_reader.hpp"
#include "progress_bar.hpp"
#include "variance_swap.hpp"

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
    //! the grid solve applies to any contract whose underlying admits a spot grid;
    //! an engine decision made here from the (pure-description) underlying, not asked
    //! of the contract.
    CheckAllowed( []( Contract* c )
                  { return c->GetUnderlying()->IsGriddable(); }, "PDE" );
}

//! solve the grid for every contract (re-runnable for the Greeks bumps)
void PricerPDE::PriceBook()
{
    //! one progress bar over the contracts; each step prices the contract and,
    //! when requested, its bump-and-revalue Greeks (see Pricer::PriceBookByContract).
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
    else if ( auto* bar = dynamic_cast<Barrier*>( Ctr ) )
    {
        PriceBarrier( bar );
    }
    else
    {
        PriceVanilla( Ctr );
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
    _r_disc = Ctr->GetPremiumCurrency()->GetRate()->GetCurveValue( _maturity );

    //! quanto drift correction (payoff currency != underlying currency): the
    //! carry becomes r - rho(S,FX)*sigma_S*sigma_X while discounting stays in the
    //! premium currency. Matches the ANA quanto forward and the MCL node so the
    //! three engines agree. rho is the signed FX-underlying correlation; the
    //! sigma_S used is the ATM implied vol (exact under a flat BS surface, an
    //! approximation for local/stochastic vol). Currency identity is by pointer
    //! (Currency objects are singletons in the book graph), so this is exact.
    Currency* udl_ccy = Ctr->GetUnderlying()->GetCurrency();
    Currency* pre_ccy = Ctr->GetPremiumCurrency();
    if ( udl_ccy != pre_ccy )
    {
        if ( !_correlation )
        {
            ERR( "book pricing '" + _name + "': quanto contract '" + Ctr->GetName() +
                 "' requires a correlation matrix" );
        }
        double v_fx = _correlation->GetFxVol( udl_ccy->GetName(), pre_ccy->GetName() );
        double rho = _correlation->GetValue( udl_ccy->GetName(), pre_ccy->GetName(), Und );
        _r -= rho * _v * v_fx;
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

    // init U_1 (boundaries)     u(x) = V(X)  X = phi(x) x =
    for ( int i = 0; i < _j + 1; i++ )
    {
        la_vector_set( U_1, i, Ctr->Intrinsic( Phi( i * _h ) ) );
    }

    //! discrete barrier observed at maturity (terminal step N)
    if ( _discrete_monitor_steps.count( _n ) )
    {
        ApplyDiscreteBarrier( U_1 );
    }

    // la_matrix_set_col (V, N, U_1);
    // PrintList(U_1);

    // diagonals of the implicit matrix T_0 (constant in time, so assembled once).
    // Rows 0 and _j are pinned to the identity (Dirichlet boundaries), interior rows
    // take the three T_0 stencil entries.
    la_vector_set( diag_u, 0, 0 );
    la_vector_set( diag_m, 0, 1 );
    la_vector_set( diag_m, _j, 1 );
    la_vector_set( diag_d, _j - 1, 0 );
    for ( int j = 1; j < _j; j++ )
    {
        la_vector_set( diag_u, j, T_0( j, j + 1 ) );     //!< super-diagonal
        la_vector_set( diag_m, j, T_0( j, j ) );         //!< main diagonal
        la_vector_set( diag_d, j - 1, T_0( j, j - 1 ) ); //!< sub-diagonal
    }
    // PrintList(diag_u);
    // PrintList(diag_m);
    // PrintList(diag_d);

    // backwarding
    for ( int i = _n - 1; i >= 0; i-- )
    {

        // right side
        la_vector_set( D_1, 0, _u_dw );  // down boundary (fonction de t?)
        la_vector_set( D_1, _j, _u_up ); // up boundary (fonction de t?)
        for ( int j = 1; j < _j; j++ )
        {
            la_vector_set( D_1, j, T_1( j, j - 1 ) * la_vector_get( U_1, j - 1 ) + T_1( j, j ) * la_vector_get( U_1, j ) + T_1( j, j + 1 ) * la_vector_get( U_1, j + 1 ) );
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
                U_0->data[k] = max( U_0->data[k], Ctr->Intrinsic( ObservedSpot( Phi( k * _h ), i ) ) );
            }
        }

        // copy U_0 to U_1 for next step
        la_vector_memcpy( U_1, U_0 );

        //! discrete barrier: zero the knocked region at scheduled steps
        if ( _discrete_monitor_steps.count( i ) )
        {
            ApplyDiscreteBarrier( U_1 );
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

    const double fair_var = ( _t_max > 0 ) ? GetGridPrice( _x_0, U_0 ) / _t_max : atm_var;
    _variance_mode = false;

    const double k_var = Ctr->GetVolatilityStrike() * Ctr->GetVolatilityStrike();
    const double df = Ctr->GetPremiumCurrency()->GetRate()->GetDiscountFactor( _maturity );
    Result( Ctr ).premium = Ctr->GetNotional() * df * ( fair_var - k_var );
    Result( Ctr ).delta = 0;
    Result( Ctr ).gamma = 0;
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
//! sign is carried so V_t = A V_XX + B V_X + C V matches the backward convention)
double PricerPDE::A( double x )
{
    return -.5 * _v * _v * x * x;
}
//! drift coefficient: -r S (carry r, in the underlying currency)
double PricerPDE::B( double x )
{
    return -_r * x;
}
//! reaction (discount) coefficient: the premium-currency discount rate r_disc...
double PricerPDE::C( double /*x*/ )
{
    //! variance-swap solve accumulates expected variance (an undiscounted
    //! expectation), so there is no reaction/discount term there.
    return _variance_mode ? 0.0 : _r_disc;
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

//! Douglas ADI for the Heston PDE
//!   V_t + 0.5 v S^2 V_SS + rho xi v S V_Sv + 0.5 xi^2 v V_vv
//!        + b S V_S + kappa(theta - v) V_v - r_d V = 0
//! with b the carry (from the forward) and r_d the premium-currency discount.
//! Cross term explicit; S- and v-sweeps implicit (theta = 1/2). European or,
//! if requested, American by intrinsic projection after each step.
PricerPDE::GridResult PricerPDE::SolveHestonGrid( Contract* Ctr )
{
    Single* single = *Ctr->GetUnderlying()->GetSingleSet().begin();
    const StochasticVolParams hv = single->GetVolatility()->StochasticParams();

    const date mat = Ctr->GetMaturityDate();
    const double T = YearFraction( _today, mat );
    const double S0 = single->GetSpot();
    const double F = Ctr->GetUnderlying()->GetForward( mat, Ctr->GetPremiumCurrency() );
    const double rd = Ctr->GetPremiumCurrency()->GetRate()->GetCurveValue( mat );
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

    //! grid extents
    const double vmax = std::max( 1.0, HESTON_VMAX_FACTOR * std::max( v0, th ) );
    const double smax = std::max( HESTON_SMAX_MIN_FACTOR * S0, S0 * exp( HESTON_SMAX_SIGMA_FACTOR * sqrt( std::max( v0, th ) * std::max( T, 1.0 ) ) ) );
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
        double vss = ( g[IX( i + 1, j )] - 2 * g[IX( i, j )] + g[IX( i - 1, j )] ) / ( dS * dS );
        double vs = ( g[IX( i + 1, j )] - g[IX( i - 1, j )] ) / ( 2 * dS );
        return 0.5 * vj * si * si * vss + bdrift * si * vs - 0.5 * rd * g[IX( i, j )];
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
        return rho * xi * V( j ) * S( i ) * vsv;
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
    for ( int n = 1; n <= Nt; n++ )
    {
        const double tau = n * dt;
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
            vector<double> a( m ), d( m ), c( m ), x( m );
            double vj = V( j );
            for ( int i = 1; i < NS; i++ )
            {
                double si = S( i );
                double diff = 0.5 * vj * si * si / ( dS * dS );
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
            vector<double> a( m ), d( m ), c( m ), x( m );
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
