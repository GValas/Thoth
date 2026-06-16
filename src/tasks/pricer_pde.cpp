#include "thoth.hpp"
#include "pricer_pde.hpp"
#include "cancellation.hpp"
#include "progress_bar.hpp"
#include "heston_volatility.hpp"

PricerPDE::PricerPDE( const string& ObjectName,
                      YamlConfig& YamlConfig ) : Pricer( ObjectName, YamlConfig )
{
}

PricerPDE::~PricerPDE() = default;

double PricerPDE::GetGridPrice( double x, gsl_vector* Uy )
{
    // x_vector (owned locally, freed on return)
    GslVector Ux = gsl_vector_alloc( Uy->size );
    for ( int j = 0; j < (int)Uy->size; j++ )
    {
        gsl_vector_set( Ux, j, j * h );
    }

    // interpolation (inputs stay owned here)
    return InterpolateWithSpline( Ux, Uy, x );
}

//! check that PDE resolution is allowed and the configuration is complete
void PricerPDE::PreCheck_()
{
    CheckAllowed( []( Contract* c )
                  { return c->PDE_HasSolution(); }, "PDE" );

    //! pde pricing needs its parameter object (config field "pde")
    if ( !_configuration->_pde )
    {
        ERR( "book pricing '" + _name + "' uses the pde method but its configuration has no 'pde' object" );
    }
}

//! solve the grid for every contract (re-runnable for the Greeks bumps)
void PricerPDE::PriceBook_()
{
    //! one progress bar over the contracts; each step prices the contract and,
    //! when requested, its bump-and-revalue Greeks (see Pricer::PriceBookByContract_).
    PriceBookByContract_( "PDE" );
}

//! single-contract grid solve hook used by the per-contract loop and its Greeks
void PricerPDE::PriceContract_( Contract* Ctr )
{
    PriceContract( Ctr );
}

//! price one contract: vanilla, knock-out, or knock-in (= vanilla - knock-out)
void PricerPDE::PriceContract( Contract* Ctr )
{

    //! Heston stochastic vol vanilla : 2-D (S, v) ADI grid
    if ( !Ctr->PDE_IsBarrier() && UnderlyingIsHeston_( Ctr ) )
    {
        GridResult r = SolveHestonGrid( Ctr );
        Ctr->SetPremium( r.premium );
        Ctr->SetDelta( r.delta );
        Ctr->SetGamma( r.gamma );
        return;
    }

    //! plain vanilla : single full-domain solve
    if ( !Ctr->PDE_IsBarrier() )
    {
        InitGrid( Ctr, false );
        GridResult r = SolveGrid( Ctr );
        Ctr->SetPremium( r.premium );
        Ctr->SetDelta( r.delta );
        Ctr->SetGamma( r.gamma );
        return;
    }

    //! discrete monitoring : full-domain solve, the knocked region zeroed only at
    //! the scheduled dates (no initial-breach shortcut: monitoring starts after
    //! today). Knock-in by in/out parity (vanilla - knock-out).
    if ( Ctr->PDE_IsDiscreteBarrier() )
    {
        InitGrid( Ctr, true );
        GridResult ko = SolveGrid( Ctr );

        GridResult res;
        if ( Ctr->PDE_IsKnockIn() )
        {
            InitGrid( Ctr, false );
            GridResult va = SolveGrid( Ctr );
            res.premium = va.premium - ko.premium;
            res.delta = va.delta - ko.delta;
            res.gamma = va.gamma - ko.gamma;
        }
        else
        {
            res = ko;
        }

        Ctr->SetPremium( res.premium );
        Ctr->SetDelta( res.delta );
        Ctr->SetGamma( res.gamma );
        return;
    }

    //! barrier already breached at valuation : knock-in = vanilla, knock-out = 0
    double H = Ctr->PDE_BarrierLevel();
    double spot = Ctr->GetUnderlying()->GetSpot();
    bool breached = Ctr->PDE_IsUpBarrier() ? ( spot >= H ) : ( spot <= H );

    GridResult res;
    if ( breached )
    {
        if ( Ctr->PDE_IsKnockIn() )
        {
            InitGrid( Ctr, false );
            res = SolveGrid( Ctr ); //!< full vanilla
        }
        //! else knocked out : res stays zero
    }
    else
    {
        //! knock-out on the clamped (live-region) domain
        InitGrid( Ctr, true );
        GridResult ko = SolveGrid( Ctr );

        if ( Ctr->PDE_IsKnockIn() )
        {
            //! knock-in by in/out parity : vanilla - knock-out
            InitGrid( Ctr, false );
            GridResult va = SolveGrid( Ctr );
            res.premium = va.premium - ko.premium;
            res.delta = va.delta - ko.delta;
            res.gamma = va.gamma - ko.gamma;
        }
        else
        {
            res = ko;
        }
    }

    Ctr->SetPremium( res.premium );
    Ctr->SetDelta( res.delta );
    Ctr->SetGamma( res.gamma );
}

//! init grid
void PricerPDE::InitGrid( Contract* Ctr, bool ApplyBarrier )
{

    // cranck-nicholson scheme
    theta = PDE_THETA;

    //
    maturity = Ctr->GetMaturityDate();
    string Und = Ctr->GetUnderlying()->GetName();
    string Ccy = Ctr->GetPremiumCurrency()->GetName();

    // market data : carry r (drift, underlying ccy) and discount r_disc (premium
    // ccy). They coincide for a non-quanto contract (same currency).
    s = Ctr->GetUnderlying()->GetSpot();
    v = Ctr->GetUnderlying()->GetImplicitVol( 0, maturity );
    r = Ctr->GetUnderlying()->GetCurrency()->GetRate()->GetCurveValue( maturity );
    r_disc = Ctr->GetPremiumCurrency()->GetRate()->GetCurveValue( maturity );

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
        r -= rho * v * v_fx;
    }

    // original grid
    T_max = YearFraction( _today, maturity );
    X_0 = s;
    X_max = s * exp( _configuration->_pde->_custom_sigma_factor * v * sqrt( T_max ) );
    X_min = s * exp( -_configuration->_pde->_custom_sigma_factor * v * sqrt( T_max ) );
    N = _configuration->_pde->_custom_n_t;
    J = _configuration->_pde->_custom_n_s;
    is_american = Ctr->PDE_IsAmerican();

    //! barrier handling
    if ( ApplyBarrier && Ctr->PDE_IsBarrier() )
    {
        bool is_up = Ctr->PDE_IsUpBarrier();
        double H = Ctr->PDE_BarrierLevel();
        if ( Ctr->PDE_IsDiscreteBarrier() )
        {
            //! discrete: keep the full domain (the knocked region survives between
            //! monitoring dates) and hold the knocked-side boundary at zero.
            if ( is_up )
            {
                V_up = 0;
                V_dw = Ctr->PDE_EvalFlow( X_min );
            }
            else
            {
                V_dw = 0;
                V_up = Ctr->PDE_EvalFlow( X_max );
            }
        }
        else
        {
            //! continuous: move the live-side boundary onto the barrier and impose
            //! a zero Dirichlet condition there.
            if ( is_up )
            {
                X_max = H;
                V_up = 0;
                V_dw = Ctr->PDE_EvalFlow( X_min );
            }
            else
            {
                X_min = H;
                V_dw = 0;
                V_up = Ctr->PDE_EvalFlow( X_max );
            }
        }
    }
    else
    {
        V_up = Ctr->PDE_EvalFlow( X_max );
        V_dw = Ctr->PDE_EvalFlow( X_min );
    }

    // transformation settings -> more points around X_0
    cc = .02;                           //.02 / .05
    c1 = asinh( ( X_min - X_0 ) / cc ); // particular case !!! [0,1] <-> [X_min, X_max]
    c2 = asinh( ( X_max - X_0 ) / cc );
    aa = c2 - c1;
    bb = c1;

    // transformed grid
    x_0 = Psi( X_0 );
    x_min = Psi( X_min );
    x_max = Psi( X_max );
    h = ( x_max - x_min ) / (double)J;
    k = T_max / (double)N;
    u_up = V_up;
    u_dw = V_dw;

    //! discrete monitoring : map each scheduled date to the nearest time step
    //! (step i is at year-fraction i*k from today). The knocked region is zeroed
    //! at these steps in SolveGrid.
    _discrete_monitor_steps.clear();
    if ( ApplyBarrier && Ctr->PDE_IsBarrier() && Ctr->PDE_IsDiscreteBarrier() )
    {
        _barrier_is_up = Ctr->PDE_IsUpBarrier();
        _barrier_level = Ctr->PDE_BarrierLevel();
        for ( const date& d : Ctr->GetFixingDates() ) //!< == the monitoring schedule
        {
            int step = (int)lround( YearFraction( _today, d ) / k );
            step = max( 0, min( N, step ) );
            _discrete_monitor_steps.insert( step );
        }
    }
}

//! zero the solved layer in the knocked region (called at each monitoring step)
void PricerPDE::ApplyDiscreteBarrier( gsl_vector* U )
{
    for ( int j = 0; j < J + 1; j++ )
    {
        double X = Phi( j * h );
        bool knocked = _barrier_is_up ? ( X >= _barrier_level ) : ( X <= _barrier_level );
        if ( knocked )
        {
            gsl_vector_set( U, j, 0 );
        }
    }
}

//!
PricerPDE::GridResult PricerPDE::SolveGrid( Contract* Ctr )
{

    // vectors & matrices allocations (RAII: freed on every exit, incl. ERR throw)
    GslVector diag_u = gsl_vector_calloc( J );     // up diag
    GslVector diag_m = gsl_vector_calloc( J + 1 ); // mid diag
    GslVector diag_d = gsl_vector_calloc( J );     // down diag
    GslVector D_1 = gsl_vector_calloc( J + 1 );    // D = T_1(j+1, j).u(j-1) + T_1(j, j).u(j) + T_1(j, j+1).u(j+1)
    GslVector U_0 = gsl_vector_calloc( J + 1 );
    GslVector U_1 = gsl_vector_calloc( J + 1 );
    // gsl_matrix * V        = gsl_matrix_calloc(J+1, N+1);

    // init U_1 (boundaries)     u(x) = V(X)  X = phi(x) x =
    for ( int i = 0; i < J + 1; i++ )
    {
        gsl_vector_set( U_1, i, Ctr->PDE_EvalFlow( Phi( i * h ) ) );
    }

    //! discrete barrier observed at maturity (terminal step N)
    if ( _discrete_monitor_steps.count( N ) )
    {
        ApplyDiscreteBarrier( U_1 );
    }

    // gsl_matrix_set_col (V, N, U_1);
    // PrintList(U_1);

    // diagonals
    gsl_vector_set( diag_u, 0, 0 );
    gsl_vector_set( diag_m, 0, 1 );
    gsl_vector_set( diag_m, J, 1 );
    gsl_vector_set( diag_d, J - 1, 0 );
    for ( int j = 1; j < J; j++ )
    {
        gsl_vector_set( diag_u, j, T_0( j, j + 1 ) );
        gsl_vector_set( diag_m, j, T_0( j, j ) );
        gsl_vector_set( diag_d, j - 1, T_0( j, j - 1 ) );
    }
    // PrintList(diag_u);
    // PrintList(diag_m);
    // PrintList(diag_d);

    // backwarding
    for ( int i = N - 1; i >= 0; i-- )
    {

        // right side
        gsl_vector_set( D_1, 0, u_dw ); // down boundary (fonction de t?)
        gsl_vector_set( D_1, J, u_up ); // up boundary (fonction de t?)
        for ( int j = 1; j < J; j++ )
        {
            gsl_vector_set( D_1, j, T_1( j, j - 1 ) * gsl_vector_get( U_1, j - 1 ) + T_1( j, j ) * gsl_vector_get( U_1, j ) + T_1( j, j + 1 ) * gsl_vector_get( U_1, j + 1 ) );
        }

        // PrintList(D_1);

        // solving system
        SolveTridiagonal( diag_m, diag_u, diag_d, D_1, U_0 );

        // american mode : max(intrinsec value, expected value)
        if ( is_american )
        {
            for ( int k = 0; k < J + 1; k++ )
            {
                U_0->data[k] = max( U_0->data[k], Ctr->PDE_EvalFlow( Phi( k * h ) ) );
            }
        }

        // copy U_0 to U_1 for next step
        gsl_vector_memcpy( U_1, U_0 );

        //! discrete barrier: zero the knocked region at scheduled steps
        if ( _discrete_monitor_steps.count( i ) )
        {
            ApplyDiscreteBarrier( U_1 );
        }
        // gsl_matrix_set_col (V, i, U_0);
    }

    // read pricer from grid
    // PrintList(U_0);
    // PrintMatrix(V);

    // read premium & spot greeks off the solved grid
    GridResult result;

    // premium, gamma
    if ( _request_premium || _request_gamma )
    {
        result.premium = GetGridPrice( x_0, U_0 );
    }

    // delta, gamma
    double delta_price_sup, delta_price_inf;
    if ( _request_delta || _request_gamma )
    {
        double x_inf = Psi( X_0 * ( 1 - GREEK_SPOT_SHIFT / 2 ) );
        double x_sup = Psi( X_0 * ( 1 + GREEK_SPOT_SHIFT / 2 ) );
        delta_price_inf = GetGridPrice( x_inf, U_0 );
        delta_price_sup = GetGridPrice( x_sup, U_0 );
        result.delta = ( delta_price_sup - delta_price_inf ) / GREEK_SPOT_SHIFT;
    }

    if ( _request_gamma )
    {
        result.gamma = ( delta_price_sup + delta_price_inf - 2 * result.premium ) /
                       ( X_0 * X_0 * GREEK_SPOT_SHIFT * GREEK_SPOT_SHIFT );
    }

    return result; //!< GslVector members above free themselves at scope exit
}

inline double PricerPDE::Phi( double x )
{
    return X_0 + cc * sinh( aa * x + bb );
}
inline double PricerPDE::Phi_x( double x )
{
    return cc * aa * cosh( aa * x + bb );
}
inline double PricerPDE::Phi_xx( double x )
{
    return cc * aa * aa * sinh( aa * x + bb );
}
inline double PricerPDE::Psi( double X )
{
    return ( asinh( ( X - X_0 ) / cc ) - bb ) / aa;
}
double PricerPDE::A( double x )
{
    return -.5 * v * v * x * x;
}
double PricerPDE::B( double x )
{
    return -r * x;
}
double PricerPDE::C( double /*x*/ )
{
    return r_disc;
}
inline double PricerPDE::a( double x )
{
    return A( Phi( x ) ) / Phi_x( x ) / Phi_x( x );
}
inline double PricerPDE::b( double x )
{
    return B( Phi( x ) ) / Phi_x( x ) - A( Phi( x ) ) * Phi_xx( x ) / Phi_x( x ) / Phi_x( x ) / Phi_x( x );
}
inline double PricerPDE::c( double x )
{
    return C( Phi( x ) );
}
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
inline double PricerPDE::L_u( int j )
{
    return b( j * h ) / 2 / h + a( j * h ) / h / h;
}
inline double PricerPDE::L_m( int j )
{
    return c( j * h ) - 2 * a( j * h ) / h / h;
}
inline double PricerPDE::L_d( int j )
{
    return -b( j * h ) / 2 / h + a( j * h ) / h / h;
}
inline double PricerPDE::I( int i, int j )
{
    return ( i == j ) ? 1 : 0;
}
inline double PricerPDE::T_0( int i, int j )
{
    return I( i, j ) + k * ( 1 - theta ) * L( i, j );
}
inline double PricerPDE::T_1( int i, int j )
{
    return I( i, j ) - k * theta * L( i, j );
}
//! ----------------------------------------------------------------------
//! Heston 2-D (S, v) finite-difference pricer (Douglas ADI)
//! ----------------------------------------------------------------------

//! true iff the contract's underlying is a mono with a stochastic-vol (Heston)
bool PricerPDE::UnderlyingIsHeston_( Contract* Ctr )
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
    HestonVolatility* hv = dynamic_cast<HestonVolatility*>( single->GetVolatility() );

    const date mat = Ctr->GetMaturityDate();
    const double T = YearFraction( _today, mat );
    const double S0 = single->GetSpot();
    const double F = Ctr->GetUnderlying()->GetForward( mat, Ctr->GetPremiumCurrency() );
    const double rd = Ctr->GetPremiumCurrency()->GetRate()->GetCurveValue( mat );
    const double b = ( T > 0 ) ? log( F / S0 ) / T : 0.0;
    const double v0 = hv->GetV0();
    const double kap = hv->GetKappa();
    const double th = hv->GetTheta();
    const double xi = hv->GetXi();
    const double rho = hv->GetRho();
    const bool american = Ctr->PDE_IsAmerican();

    //! grid extents
    const double vmax = std::max( 1.0, 6.0 * std::max( v0, th ) );
    const double smax = std::max( 3.0 * S0, S0 * exp( 5.0 * sqrt( std::max( v0, th ) * std::max( T, 1.0 ) ) ) );
    const int NS = 120;
    const int Nv = 60;
    const int Nt = 120;
    const double dS = smax / NS;
    const double dv = vmax / Nv;
    const double dt = T / Nt;
    const double q = 0.5; //!< ADI implicitness (Crank-Nicolson directional)

    auto S = [&]( int i ) { return i * dS; };
    auto V = [&]( int i ) { return i * dv; };
    auto IX = [&]( int i, int j ) { return i * ( Nv + 1 ) + j; };

    vector<double> U( ( NS + 1 ) * ( Nv + 1 ), 0.0 );

    //! terminal payoff
    for ( int i = 0; i <= NS; i++ )
    {
        double payoff = Ctr->PDE_EvalFlow( S( i ) );
        for ( int j = 0; j <= Nv; j++ )
        {
            U[IX( i, j )] = payoff;
        }
    }
    const double intrinsic0 = Ctr->PDE_EvalFlow( 0.0 ); //!< payoff at S=0 (0 call, K put)

    //! per-step operator applications and tridiagonal coefficients
    auto A1 = [&]( int i, int j, const vector<double>& g ) //!< S-direction (+ half discount)
    {
        double si = S( i ), vj = V( j );
        double vss = ( g[IX( i + 1, j )] - 2 * g[IX( i, j )] + g[IX( i - 1, j )] ) / ( dS * dS );
        double vs = ( g[IX( i + 1, j )] - g[IX( i - 1, j )] ) / ( 2 * dS );
        return 0.5 * vj * si * si * vss + b * si * vs - 0.5 * rd * g[IX( i, j )];
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
            g[IX( 0, j )] = intrinsic0 * exp( -rd * tau );             //!< S=0 (Dirichlet)
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
                Y0[IX( i, j )] = U[IX( i, j )] + dt * ( A0( i, j, U ) + A1( i, j, U ) + A2( i, j, U ) );
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
                double drift = b * si / ( 2 * dS );
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
                double ev = Ctr->PDE_EvalFlow( S( i ) );
                for ( int j = 0; j <= Nv; j++ )
                {
                    U[IX( i, j )] = std::max( U[IX( i, j )], ev );
                }
            }
        }
    }

    //! interpolate the price at (S0, v0): bilinear on the grid
    int i0 = std::min( NS - 1, std::max( 0, (int)( S0 / dS ) ) );
    int j0 = std::min( Nv - 1, std::max( 0, (int)( v0 / dv ) ) );
    double ws = ( S0 - S( i0 ) ) / dS, wv = ( v0 - V( j0 ) ) / dv;
    auto at = [&]( int i, int j ) { return U[IX( i, j )]; };
    double price = ( 1 - ws ) * ( 1 - wv ) * at( i0, j0 ) + ws * ( 1 - wv ) * at( i0 + 1, j0 ) +
                   ( 1 - ws ) * wv * at( i0, j0 + 1 ) + ws * wv * at( i0 + 1, j0 + 1 );

    //! delta / gamma by finite differences in S at v0 (central, one grid step)
    auto price_at = [&]( int i ) {
        return ( 1 - wv ) * at( i, j0 ) + wv * at( i, j0 + 1 );
    };
    GridResult res;
    res.premium = price;
    res.delta = ( price_at( i0 + 1 ) - price_at( i0 - 1 < 0 ? 0 : i0 - 1 ) ) / ( 2 * dS );
    res.gamma = ( price_at( i0 + 1 ) - 2 * price_at( i0 ) + price_at( i0 - 1 < 0 ? 0 : i0 - 1 ) ) / ( dS * dS );
    return res;
}
