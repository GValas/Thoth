#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Model-parameter Greeks vega_<param> (Pricer::ComputeParamGreeks): for each
// requested parameter, bump it on every vol surface that exposes it, reprice the
// book and take the one-sided finite difference. Priced with the ANA engine (exact
// closed form), so each sensitivity is clean. Checks: the requested params are
// produced and finite, the level sensitivities have the right sign, and a
// parameter no surface in the book exposes is silently skipped (no result key).

namespace
{
//! does the result block carry this key?
bool Has( const YAML::Node& r, const std::string& key )
{
    return static_cast<bool>( r["res"][key] );
}

//! a Heston vanilla. rho (spot/variance) is carried via the eq_var pseudo-udl.
std::string HestonCfg( const std::string& indicators )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: bk, currency: eur, configuration: cfg,"
      << " correlation: cor, indicators: " << indicators << ", result: res}\n"
      << "cfg: !pricer_configuration {method: ana, mcl_configuration: m, log_path: \"/tmp/\"}\n"
      << "m: !mcl_configuration {max_day_step: 3, min_day_step: -1, paths: 1,"
      << " vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
      << "h: !heston_volatility {spot: 100, init_vol: 30, long_vol: 20, kappa: 1.5,"
      << " vol_of_vol: 40, jump_intensity: 0, jump_mean: 0, jump_vol: 0, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, -0.7, -0.7, 1]}\n"
      << "bk: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " maturity: 2000-12-31, type: call, exercise: european}\n";
    return o.str();
}

//! a SABR (local-vol) vanilla priced by the ANA engine at the strike's SABR vol.
std::string SabrCfg( const std::string& indicators )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: bk, currency: eur, configuration: cfg,"
      << " correlation: cor, indicators: " << indicators << ", result: res}\n"
      << "cfg: !pricer_configuration {method: ana, mcl_configuration: m, log_path: \"/tmp/\"}\n"
      << "m: !mcl_configuration {max_day_step: 3, min_day_step: -1, paths: 1,"
      << " vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: s, currency: eur}\n"
      << "s: !sabr_volatility {maturities: [5.0], alpha: [0.30], beta: [1.0],"
      << " rho: [-0.3], nu: [0.4], calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "bk: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " maturity: 2000-12-31, type: call, exercise: european}\n";
    return o.str();
}
} // namespace

TEST_CASE( "Heston vega_<param> Greeks are produced, finite, and v0 is positive" )
{
    auto res = Price( HestonCfg( "[premium, vega_v0, vega_kappa, vega_theta, vega_xi, vega_rho]" ) );

    for ( std::string p : { "vega_v0", "vega_kappa", "vega_theta", "vega_xi", "vega_rho" } )
    {
        CAPTURE( p );
        REQUIRE( Has( res, p ) );
        CHECK( std::isfinite( Greek( res, p ) ) );
    }
    //! more initial variance -> a more valuable ATM call
    CHECK( Greek( res, "vega_v0" ) > 0 );
}

TEST_CASE( "SABR vega_alpha is positive (raising the vol level lifts the call)" )
{
    auto res = Price( SabrCfg( "[premium, vega_alpha, vega_beta, vega_rho, vega_nu]" ) );

    for ( std::string p : { "vega_alpha", "vega_beta", "vega_rho", "vega_nu" } )
    {
        CAPTURE( p );
        REQUIRE( Has( res, p ) );
        CHECK( std::isfinite( Greek( res, p ) ) );
    }
    CHECK( Greek( res, "vega_alpha" ) > 0 );
}

TEST_CASE( "a parameter no surface exposes is silently skipped" )
{
    //! vega_alpha is a SABR parameter; a Heston book has no surface exposing it,
    //! so ComputeParamGreeks skips it and no vega_alpha result key is written.
    auto res = Price( HestonCfg( "[premium, vega_v0, vega_alpha]" ) );
    CHECK( Has( res, "vega_v0" ) );
    CHECK_FALSE( Has( res, "vega_alpha" ) );
}
