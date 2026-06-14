#include "thoth.hpp"
#include "pricer_pde.hpp"
#include "cancellation.hpp"
#include "progress_bar.hpp"

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

    // vectors & matrices allocations
    gsl_vector* diag_u = gsl_vector_calloc( J );     // up diag
    gsl_vector* diag_m = gsl_vector_calloc( J + 1 ); // mid diag
    gsl_vector* diag_d = gsl_vector_calloc( J );     // down diag
    gsl_vector* D_1 = gsl_vector_calloc( J + 1 );    // D = T_1(j+1, j).u(j-1) + T_1(j, j).u(j) + T_1(j, j+1).u(j+1)
    gsl_vector* U_0 = gsl_vector_calloc( J + 1 );
    gsl_vector* U_1 = gsl_vector_calloc( J + 1 );
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
        gsl_linalg_solve_tridiag( diag_m, diag_u, diag_d, D_1, U_0 );

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

    // free memory
    // gsl_matrix_free(V);
    gsl_vector_free( diag_u );
    gsl_vector_free( diag_m );
    gsl_vector_free( diag_d );
    gsl_vector_free( D_1 );
    gsl_vector_free( U_0 );
    gsl_vector_free( U_1 );

    return result;
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