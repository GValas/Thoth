//! sabr_volatility.cpp — SABR implied-vol surface via Hagan's 2002 expansion.
//!
//! The SABR model is dF = alpha F^beta dW, d alpha = nu alpha dZ, d<W,Z> = rho dt.
//! Hagan et al. give a closed-form asymptotic for the Black (lognormal) implied
//! vol as a function of (F, K, T) and the 4 params (alpha, beta, rho, nu). Here
//! those params are term-structured (one set per maturity, linearly interpolated)
//! and the surface is consumed as a local-vol surface (Dupire) by the PDE pricer.

#include "thoth.hpp"
#include "sabr_volatility.hpp"
#include "object_reader.hpp"

//! constructor: SABR is exposed as a *local-vol* surface, so the PDE pricer runs
//! it through GetLocalVolatility (Dupire) rather than reading a single number.
SabrVolatility::SabrVolatility( const string& ObjectName ) : Volatility( ObjectName, KIND_SABR_VOLATILITY )
{
    _is_local = true; //!< Dupire-transformed by the PDE; no constant-vol MC node
}

//!
SabrVolatility::~SabrVolatility() = default;

//! read own fields (per-maturity SABR params), then the common calendar
void SabrVolatility::Configure( ObjectReader& reader )
{
    Volatility::Configure( reader ); //!< common fields first (optional calendar)
    //! each list is one SABR parameter sampled at the maturities in _maturity_list
    _maturity_list = reader.Get<vector<double>>( "maturities" );
    _alpha_list = reader.Get<vector<double>>( "alpha" );
    _beta_list = reader.Get<vector<double>>( "beta" );
    _rho_list = reader.Get<vector<double>>( "rho" );
    _nu_list = reader.Get<vector<double>>( "nu" );
}

//! linear interpolation in time of one parameter list, flat-extrapolated beyond
//! the first/last quoted maturity. @param Values the per-maturity samples (same
//! length/order as _maturity_list), @param T target time in years.
double SabrVolatility::Interp( const vector<double>& Values, double T ) const
{
    if ( T <= _maturity_list.front() )
    {
        return Values.front(); //!< flat before the first pillar
    }
    if ( T >= _maturity_list.back() )
    {
        return Values.back(); //!< flat after the last pillar
    }
    //! find the bracketing pillars [i-1, i] and lerp with weight w in [0,1)
    for ( size_t i = 1; i < _maturity_list.size(); i++ )
    {
        if ( T <= _maturity_list[i] )
        {
            double w = ( T - _maturity_list[i - 1] ) / ( _maturity_list[i] - _maturity_list[i - 1] );
            return Values[i - 1] + w * ( Values[i] - Values[i - 1] );
        }
    }
    return Values.back(); //!< unreachable (T < back() guaranteed above)
}

//! Hagan (2002) lognormal SABR implied volatility
double SabrVolatility::GetImplicitVol( const double Strike,
                                       const double Forward,
                                       const date& MaturityDate )
{
    double T = YearFraction( _today, MaturityDate );
    if ( T <= 0 )
    {
        T = 1.0 / NB_OF_DAYS_A_YEAR; //!< guard for same-day maturity bumps
    }
    double F = Forward; //!< Hagan's formula is in the forward measure
    //! the engine passes Strike = 0 as a sentinel for the reference (ATM) vol
    double K = ( Strike > 0 ) ? Strike : F;

    //! quoted parameters + any vega_<param> bump (the shift is 0 in normal pricing)
    double alpha = Interp( _alpha_list, T ) + ParamShift( "alpha" );
    double beta = Interp( _beta_list, T ) + ParamShift( "beta" );
    double rho = Interp( _rho_list, T ) + ParamShift( "rho" );
    double nu = Interp( _nu_list, T ) + ParamShift( "nu" );

    //! Hagan building blocks. The implied vol factorises as
    //!   sigma_B(K,F) = (alpha / den) * (z / x(z)) * (1 + [...] T)
    double one_mb = 1.0 - beta;                 //!< (1 - beta), recurs throughout
    double log_fk = log( F / K );               //!< log-moneyness
    double fk_pow = pow( F * K, one_mb / 2.0 ); //!< (F K)^{(1-beta)/2}

    //! leading denominator: the (1-beta) series in log(F/K) (Hagan eq. for the
    //! "geometric-mean" pre-factor); -> fk_pow as K -> F (ATM)
    double den = fk_pow * ( 1.0 + one_mb * one_mb / 24.0 * log_fk * log_fk +
                            one_mb * one_mb * one_mb * one_mb / 1920.0 * log_fk * log_fk * log_fk * log_fk );

    //! the z / x(z) wing factor captures the skew from vol-of-vol; z/x(z) -> 1 as
    //! z -> 0, so guard the ATM limit explicitly to avoid 0/0.
    double z = ( nu / alpha ) * fk_pow * log_fk;
    double zxz = 1.0;
    if ( fabs( z ) > 1e-12 )
    {
        double xz = log( ( sqrt( 1.0 - 2.0 * rho * z + z * z ) + z - rho ) / ( 1.0 - rho ) );
        zxz = z / xz;
    }

    //! time-correction bracket (multiplies T): three O(T) terms — CEV curvature
    //! (beta), the alpha-nu-rho cross term (skew), and the pure vol-of-vol term.
    double corr = 1.0 + ( one_mb * one_mb / 24.0 * alpha * alpha / pow( F * K, one_mb ) +
                          0.25 * rho * beta * nu * alpha / fk_pow +
                          ( 2.0 - 3.0 * rho * rho ) / 24.0 * nu * nu ) *
                            T;

    //! assemble Hagan's vol, then add the parallel vega bump (0 in normal pricing)
    return ( alpha / den ) * zxz * corr + _vol_shift;
}

//! SABR is a local-vol surface for the PDE pricer : no Monte-Carlo node
MonteCarloNode* SabrVolatility::GetNode( NodeCollector& /*NC*/ )
{
    ERR( "sabr_volatility '" + _name + "' : Monte-Carlo node not supported (use the PDE pricer)" );
}
