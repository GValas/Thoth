#include "finance.hpp"
#include <complex>
#include <gsl/gsl_integration.h>

//! vanilla price
double payoff_vanilla( const double spot,
                       const double strike,
                       const OptionType type,
                       const bool has_cap,
                       const double cap,
                       const bool has_floor,
                       const double floor )
{
    //! vanilla part
    double vanilla = ( type == OptionType::Call ) ? ( spot - strike ) : ( strike - spot );

    //! cap/floor
    if ( has_cap )
    {
        vanilla = min( vanilla, cap );
    }
    if ( has_floor )
    {
        vanilla = max( vanilla, floor );
    }

    return vanilla;
}

double payoff_digital( const double spot,
                       const string& barrier_type,
                       const double barrier_up_level,
                       const double barrier_down_level )
{
    double digital;
    if ( barrier_type == BARRIER_TYPE_UP_AND_OUT )
    {
        digital = ( spot >= barrier_up_level ) ? 0 : 1;
    }
    else if ( barrier_type == BARRIER_TYPE_UP_AND_IN )
    {
        digital = ( spot >= barrier_up_level ) ? 1 : 0;
    }
    else if ( barrier_type == BARRIER_TYPE_DOWN_AND_OUT )
    {
        digital = ( spot <= barrier_down_level ) ? 0 : 1;
    }
    else if ( barrier_type == BARRIER_TYPE_DOWN_AND_IN )
    {
        digital = ( spot <= barrier_down_level ) ? 1 : 0;
    }
    else
    {
        ERR( "ERR> unknown barrier type '" + barrier_type + "'" );
    }
    return digital;
}

//! bs call price
double BS_Call_Price( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return DiscountFactor * max( Forward - Strike, 0.0 );
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double d2 = d1 - v_sqr_t;
        double Nd1 = gsl_cdf_ugaussian_P( d1 );
        double Nd2 = gsl_cdf_ugaussian_P( d2 );
        return DiscountFactor * ( Forward * Nd1 - Strike * Nd2 );
    }
}

//! bs put price, call/put parity
double BS_Put_Price( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor )
{
    double c = BS_Call_Price( Forward, Strike, TimeToMaturity, Volatility, DiscountFactor );
    double p = c - DiscountFactor * ( Forward - Strike );
    return p;
}

//! bs call delta
double BS_Call_Delta( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return ( Forward > Strike ? DiscountFactor : 0 );
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double Nd1 = gsl_cdf_ugaussian_P( d1 );
        return DiscountFactor * Nd1;
    }
}

//! bs put delta, call/put parity
double BS_Put_Delta( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor )
{
    double c = BS_Call_Delta( Forward, Strike, TimeToMaturity, Volatility, DiscountFactor );
    double p = c - 1;
    return p;
}

//! bs vega
double BS_Vega( const double Forward,
                const double Strike,
                const double TimeToMaturity,
                const double Volatility,
                const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return 0;
    }
    else
    {
        double sqr_t = sqrt( TimeToMaturity );
        double v_sqr_t = Volatility * sqr_t;
        double d2 = log( Forward / Strike ) / v_sqr_t - 0.5 * v_sqr_t;
        double Fd2 = gsl_ran_ugaussian_pdf( d2 );
        return DiscountFactor * Strike * sqr_t * Fd2;
    }
}

//! bs gamma
double BS_Gamma( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return 0;
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double Fd1 = gsl_ran_ugaussian_pdf( d1 );
        return DiscountFactor * Fd1 / Forward / v_sqr_t;
    }
}

//! bs volga
double BS_Volga( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return 0;
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double d2 = d1 - v_sqr_t;
        double Fd1 = gsl_ran_ugaussian_pdf( d1 );
        return DiscountFactor * Forward * Fd1 * v_sqr_t * d1 * d2;
    }
}

// implicit vol
double BS_Call_ImplicitVol( const double Forward,
                            const double Strike,
                            const double TimeToMaturity,
                            const double Price,
                            const double DiscountFactor )
{

    //! vol start point
    double vega, vol_error, vol = INITIAL_IMPLICIT_VOL;
    int i = 0;
    do
    {
        if ( !( vega = BS_Vega( Forward, Strike, TimeToMaturity, vol, DiscountFactor ) ) )
        {
            //! zero vega: Newton cannot proceed — fail loudly rather than returning a -1 "vol"
            ERR( "BS_Call_ImplicitVol: zero vega, cannot invert price " + ToString( Price ) );
        }
        else
        {
            vol_error = Price - BS_Call_Price( Forward, Strike, TimeToMaturity, vol, DiscountFactor );
            vol += vol_error / vega;
        }
    } while ( abs( vol_error ) > IMPLICIT_VOL_MAX_ERROR && ++i < IMPLICIT_VOL_MAX_ITERATIONS );

    return vol;
}
//! ----------------------------------------------------------------------
//! Heston European pricing via the characteristic function
//! ----------------------------------------------------------------------
namespace
{
using cdouble = std::complex<double>;

struct HestonParams
{
    double F, K, T, v0, kappa, theta, xi, rho;
    double lambda, muJ, sigmaJ; //!< Bates lognormal jumps (lambda = 0 -> pure Heston)
    int j;                      //!< 1 or 2 (the two Heston probabilities P1, P2)
};

//! integrand of P_j : Re( e^{-i phi ln K} f_j(phi) / (i phi) ), forward measure
//! (x = ln F, no drift term — the forward carries the drift).
double heston_integrand( double phi, void* params )
{
    const HestonParams& h = *static_cast<HestonParams*>( params );
    const cdouble I( 0.0, 1.0 );

    const double uj = ( h.j == 1 ) ? 0.5 : -0.5;
    const double bj = ( h.j == 1 ) ? ( h.kappa - h.rho * h.xi ) : h.kappa;
    const double a = h.kappa * h.theta;
    const double xi2 = h.xi * h.xi;

    const cdouble rxi = h.rho * h.xi * I * phi;
    const cdouble d = std::sqrt( ( rxi - bj ) * ( rxi - bj ) - xi2 * ( 2.0 * uj * I * phi - phi * phi ) );
    const cdouble g = ( bj - rxi - d ) / ( bj - rxi + d ); //!< little-Heston-trap
    const cdouble edt = std::exp( -d * h.T );

    const cdouble C = ( a / xi2 ) * ( ( bj - rxi - d ) * h.T - 2.0 * std::log( ( 1.0 - g * edt ) / ( 1.0 - g ) ) );
    const cdouble D = ( bj - rxi - d ) / xi2 * ( ( 1.0 - edt ) / ( 1.0 - g * edt ) );
    cdouble f = std::exp( C + D * h.v0 + I * phi * std::log( h.F ) );

    //! Bates : multiply by the closed-form jump characteristic function. The two
    //! probabilities are the CF under different measures, encoded here by the
    //! argument shift u = phi (P2) vs phi - i (P1); the compensator (-i u kbar)
    //! makes the jump factor martingale-neutral, so it equals 1 at u = -i.
    if ( h.lambda > 0 )
    {
        const cdouble u = ( h.j == 1 ) ? ( phi - I ) : cdouble( phi );
        const double kbar = exp( h.muJ + 0.5 * h.sigmaJ * h.sigmaJ ) - 1.0;
        const cdouble psi = std::exp( I * u * h.muJ - 0.5 * u * u * h.sigmaJ * h.sigmaJ ) - 1.0 - I * u * kbar;
        f *= std::exp( h.T * h.lambda * psi );
    }

    return std::real( std::exp( -I * phi * std::log( h.K ) ) * f / ( I * phi ) );
}

//! P_j = 1/2 + 1/pi * integral_0^inf integrand dphi
double heston_probability( HestonParams h, int j )
{
    h.j = j;
    gsl_integration_workspace* w = gsl_integration_workspace_alloc( 1000 );
    gsl_function fn;
    fn.function = &heston_integrand;
    fn.params = &h;
    double result = 0;
    double err = 0;
    //! semi-infinite integral from a small epsilon (integrand is regular at 0)
    gsl_integration_qagiu( &fn, 1e-8, 1e-8, 1e-8, 1000, w, &result, &err );
    gsl_integration_workspace_free( w );
    return 0.5 + result / M_PI;
}
} // namespace

double Heston_Call_Price( const double Forward,
                          const double Strike,
                          const double TimeToMaturity,
                          const double DiscountFactor,
                          const double V0,
                          const double Kappa,
                          const double Theta,
                          const double Xi,
                          const double Rho,
                          const double JumpIntensity,
                          const double JumpMean,
                          const double JumpVol )
{
    //! degenerate (no vol-of-vol / zero maturity) : flat-vol Black-Scholes. The
    //! characteristic-function integrand divides by xi^2, so Xi must stay > 0;
    //! the Bates jumps require the CF, hence xi > 0 too.
    if ( Xi <= 1e-10 || TimeToMaturity <= 0 || Forward <= 0 || Strike <= 0 )
    {
        return BS_Call_Price( Forward, Strike, TimeToMaturity, sqrt( max( V0, 0.0 ) ), DiscountFactor );
    }
    HestonParams h{ Forward, Strike, TimeToMaturity, V0, Kappa, Theta, Xi, Rho,
                    JumpIntensity, JumpMean, JumpVol, 1 };
    double P1 = heston_probability( h, 1 );
    double P2 = heston_probability( h, 2 );
    return DiscountFactor * ( Forward * P1 - Strike * P2 );
}

double Heston_Put_Price( const double Forward,
                         const double Strike,
                         const double TimeToMaturity,
                         const double DiscountFactor,
                         const double V0,
                         const double Kappa,
                         const double Theta,
                         const double Xi,
                         const double Rho,
                         const double JumpIntensity,
                         const double JumpMean,
                         const double JumpVol )
{
    //! put/call parity
    double c = Heston_Call_Price( Forward, Strike, TimeToMaturity, DiscountFactor,
                                  V0, Kappa, Theta, Xi, Rho, JumpIntensity, JumpMean, JumpVol );
    return c - DiscountFactor * ( Forward - Strike );
}
