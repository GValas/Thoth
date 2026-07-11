#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Hull-White 1F equity-rate hybrid: a !currency carries a `rate_model` (a
// !hull_white with mean_reversion a and volatility sigma_r); the MCL diffuses
// the exact OU factor x, discounts pathwise with exp(-int r) (trapezoid int x +
// the V/2 convexity fitting the curve) and drifts the equity at the stochastic
// rate; the ANA prices the BS+HW European vanilla with the effective variance
//   sigma_eff^2 T = sigma_S^2 T + 2 rho sigma_S sigma_r int B + sigma_r^2 int B^2.
// The equity/rate correlation rho is the matrix entry against "<ccy>_ir".

namespace
{

//! one European option on a BS equity whose currency carries a HW model
//! (projection 8%, OIS discounting 5% — the hybrid composes with multi-curve)
std::string HwCfg( const std::string& method, double rho, double a,
                   double sigma_r_pct, const std::string& type, int draws = 1,
                   int max_day_step = 30 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << "cfg_mcl: !mcl_configuration {max_day_step: " << max_day_step
      << ", min_day_step: -1, paths: " << draws << ", vol_year_step: 0.01, use_sobol: true}\n"
      << "cfg_pde: !pde_configuration {vanilla_precision: high}\n"
      << "eur: !currency {rate: proj_curve, discount_rate: ois_curve, rate_model: hw}\n"
      << "hw: !hull_white {mean_reversion: " << a << ", volatility: " << sigma_r_pct << "}\n"
      << "proj_curve: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "ois_curve: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eur_ir],"
      << " matrix: [1, " << rho << ", " << rho << ", 1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: european}\n";
    return o.str();
}

//! test-side effective vol: sigma_eff^2 T = v^2 T + 2 rho v s int B + s^2 int B^2
double EffVol( double v, double rho, double a, double s, double T )
{
    const double b = ( 1 - std::exp( -a * T ) ) / a;
    const double c = ( 1 - std::exp( -2 * a * T ) ) / ( 2 * a );
    const double int_b = ( T - b ) / a;
    const double int_b2 = ( T - 2 * b + c ) / ( a * a );
    return std::sqrt( v * v + ( 2 * rho * v * s * int_b + s * s * int_b2 ) / T );
}

//! Black on a forward: df * (F N(d1) - K N(d2))
double Black( double F, double K, double df, double sig, double T )
{
    const double sd = sig * std::sqrt( T );
    const double d1 = std::log( F / K ) / sd + sd / 2;
    return df * ( F * NormCdf( d1 ) - K * NormCdf( d1 - sd ) );
}

} // namespace

// The ANA engine must reproduce the test-side BS+HW closed form: Black on the
// (unchanged) multi-curve forward and OIS df, at the effective vol. And the MCL
// hybrid diffusion must converge to the same price for each correlation sign.
TEST_CASE( "hull-white: ANA matches the effective-vol closed form, MCL matches ANA" )
{
    const double a = 0.10, s = 0.02; // mean reversion 10%, sigma_r 2%
    const double F = 100 * std::exp( 0.08 * T1 ), df = std::exp( -0.05 * T1 );

    for ( double rho : { -0.5, 0.0, 0.5 } )
    {
        CAPTURE( rho );
        const double ref = Black( F, 100, df, EffVol( 0.30, rho, a, s, T1 ), T1 );

        double ana = Premium( Price( HwCfg( "ana", rho, a, 100 * s, "call" ) ) );
        CHECK( std::abs( ana - ref ) <= 1e-6 ); //!< same closed form

        auto mr = Price( HwCfg( "mcl", rho, a, 100 * s, "call", 150000, 7 ) );
        double mcl = Premium( mr );
        //! trapezoid int x bias is second order in the (7-day) step
        CHECK( std::abs( mcl - ref ) <= 6.0 * Trust( mr ) + 2e-2 );
    }
}

// Put-call parity is pathwise under the hybrid (the discount and the spot share
// the same exponent, which cancels in D*S): call - put must equal
// df_ois * (F - K) — this pins E[exp(-int r)] = P(0,T) (the V/2 convexity) and
// the unchanged forward in one identity.
TEST_CASE( "hull-white: put-call parity pins the stochastic discounting" )
{
    const double a = 0.10, s = 0.02, rho = 0.5;
    const double F = 100 * std::exp( 0.08 * T1 ), df = std::exp( -0.05 * T1 );

    auto mc = Price( HwCfg( "mcl", rho, a, 100 * s, "call", 150000, 7 ) );
    auto mp = Price( HwCfg( "mcl", rho, a, 100 * s, "put", 150000, 7 ) );
    const double parity = Premium( mc ) - Premium( mp );
    CHECK( std::abs( parity - df * ( F - 100 ) ) <=
           6.0 * ( Trust( mc ) + Trust( mp ) ) + 2e-2 );
}

// sigma_r = 0 collapses the hybrid to the deterministic multi-curve price.
TEST_CASE( "hull-white: zero rate vol reduces to the multi-curve Black-Scholes" )
{
    const double F = 100 * std::exp( 0.08 * T1 ), df = std::exp( -0.05 * T1 );
    const double bs = Black( F, 100, df, 0.30, T1 );
    double ana = Premium( Price( HwCfg( "ana", 0.0, 0.10, 0.0, "call" ) ) );
    CHECK( ana == doctest::Approx( bs ).epsilon( 1e-10 ) );
}

// Out-of-scope combinations must fail loudly instead of mispricing.
TEST_CASE( "hull-white: unsupported engine / contract combinations are rejected" )
{
    //! PDE: no 2-D (S, r) grid yet
    CHECK_THROWS_AS( Price( HwCfg( "pde", 0.0, 0.10, 2.0, "call" ) ),
                     std::runtime_error );

    //! MCL American: the LSM pass discounts deterministically
    {
        std::string cfg = HwCfg( "mcl", 0.0, 0.10, 2.0, "put", 100 );
        const std::string from = "exercise: european", to = "exercise: american";
        cfg.replace( cfg.find( from ), from.size(), to );
        CHECK_THROWS_AS( Price( cfg ), std::runtime_error );
    }

    //! negative mean reversion is rejected at load
    {
        std::string cfg = HwCfg( "ana", 0.0, 0.10, 2.0, "call" );
        const std::string from = "mean_reversion: 0.1", to = "mean_reversion: -0.1";
        cfg.replace( cfg.find( from ), from.size(), to );
        CHECK_THROWS_AS( Price( cfg ), std::runtime_error );
    }
}
