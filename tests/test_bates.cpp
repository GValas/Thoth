#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Bates = Heston + lognormal jumps. The ANA pricer adds a closed-form jump factor
// to the characteristic function; the MCL pricer adds a compound-Poisson jump to
// the Andersen-QE spot. They must agree, jumps must add value, and lambda = 0 must
// recover pure Heston.
namespace
{
//! one Heston/Bates vanilla. lambda = 0 -> pure Heston. rho (spot/variance) is
//! carried through the correlation matrix via the eq_var pseudo-underlying.
std::string BatesCfg( const std::string& method, double lambda, double mu, double sigma,
                      const std::string& type = "call", int draws = 400000 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: bk, currency: eur, configuration: cfg,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "cfg: !pricer_configuration {method: " << method << ", mcl_configuration: m, log_path: \"/tmp/\"}\n"
      << "m: !mcl_configuration {max_time_step: 3, min_time_step: -1, paths: " << draws
      << ", vol_time_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
      << "h: !heston_volatility {spot: 100, init_vol: 25, long_vol: 25, kappa: 1.5, vol_of_vol: 40,"
      << " jump_intensity: " << lambda << ", jump_mean: " << mu << ", jump_vol: " << sigma
      << ", calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, -0.7, -0.7, 1]}\n"
      << "bk: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100, maturity: 2000-12-31,"
      << " type: " << type << ", exercise: european}\n";
    return o.str();
}
} // namespace

// lambda = 0 : the jump factor is 1, so Bates collapses to pure Heston (ANA path
// is literally the same characteristic function).
TEST_CASE( "Bates with zero jump intensity equals Heston" )
{
    const double heston = Premium( Price( BatesCfg( "ana", 0, 0, 0 ) ) );
    const double bates0 = Premium( Price( BatesCfg( "ana", 0, -0.10, 0.15 ) ) ); //!< lambda=0
    CHECK( std::abs( heston - bates0 ) <= 1e-9 );
}

// The ANA characteristic-function jump factor and the MCL compound-Poisson jump
// must price the same Bates option (within Monte-Carlo error).
TEST_CASE( "Bates ANA matches MCL" )
{
    auto ana = Price( BatesCfg( "ana", 0.5, -0.10, 0.15 ) );
    auto mcl = Price( BatesCfg( "mcl", 0.5, -0.10, 0.15 ) );
    CAPTURE( Premium( ana ) );
    CAPTURE( Premium( mcl ) );
    CHECK( std::abs( Premium( ana ) - Premium( mcl ) ) <= 6.0 * Trust( mcl ) + 5e-2 );
}

// Adding jumps (here downward-biased, crash-like) raises the option value over
// pure Heston: extra variance plus a fatter left tail.
TEST_CASE( "Bates jumps add value over Heston" )
{
    const double heston = Premium( Price( BatesCfg( "ana", 0, 0, 0 ) ) );
    const double bates = Premium( Price( BatesCfg( "ana", 0.6, -0.10, 0.20 ) ) );
    CAPTURE( heston );
    CAPTURE( bates );
    CHECK( bates > heston + 0.5 );
}

// Put/call parity holds for the Bates ANA pricer: C - P = df*(F - K).
TEST_CASE( "Bates put/call parity (ANA)" )
{
    const double call = Premium( Price( BatesCfg( "ana", 0.5, -0.10, 0.15, "call" ) ) );
    const double put = Premium( Price( BatesCfg( "ana", 0.5, -0.10, 0.15, "put" ) ) );
    // F = 100*exp(0.05*1), K = 100, df = exp(-0.05)
    const double F = 100.0 * std::exp( 0.05 * T1 );
    const double df = std::exp( -0.05 * T1 );
    CHECK( std::abs( ( call - put ) - df * ( F - 100.0 ) ) <= 1e-6 );
}
