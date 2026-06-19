#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Term-structured discounting / drift. On a STEEP yield curve the Monte-Carlo spot
// must grow at the forward carry implied by the whole curve, not at a single
// front-pillar rate. Before the fix the MCL drift used the flat front-pillar rate,
// so a long-dated forward (and hence the option) was mispriced versus the analytic
// and PDE engines, which both build the forward from the zero rate to maturity.
// With the term-structured rate node the three engines agree again.

namespace
{
//! one 3y European call on a flat-vol equity, priced under a steep rate curve
//! (2% at the front, 10% at the long end). method selects the engine.
std::string TsCfg( const std::string& method )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << "cfg: !pricer_configuration {method: " << method
      << ", mcl_configuration: cfg_mcl, pde_configuration: cfg_pde, log_path: \"/tmp/\"}\n"
      << "cfg_mcl: !mcl_configuration {max_day_step: 7, min_day_step: -1,"
      << " paths: 400000, vol_year_step: 0.01, use_sobol: true}\n"
      << "cfg_pde: !pde_configuration {vanilla_precision: high}\n"
      << "eur: !currency {rate: rate}\n"
      //! steep curve: 2% at t=0 rising to 10% at the 4y pillar
      << "rate: !yield_curve {dates: [2000-01-01, 2004-01-01], values: [2, 10]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 25, calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2003-01-01, nominal: 1,"
      << " type: call, exercise: european}\n";
    return o.str();
}
} // namespace

TEST_CASE( "MCL reproduces the analytic price under a steep rate curve" )
{
    const double ana = Premium( Price( TsCfg( "ana" ) ) );
    auto mcl = Price( TsCfg( "mcl" ) );
    CAPTURE( ana );
    CAPTURE( Premium( mcl ) );
    //! the front-pillar-drift bug underpriced this call by several currency units
    //! (~6 on a ~17 premium); the term-structured drift brings MCL within MC error
    CHECK( Premium( mcl ) == doctest::Approx( ana ).epsilon( 0.01 ) );
}

TEST_CASE( "PDE reproduces the analytic price under a steep rate curve" )
{
    const double ana = Premium( Price( TsCfg( "ana" ) ) );
    const double pde = Premium( Price( TsCfg( "pde" ) ) );
    CAPTURE( ana );
    CAPTURE( pde );
    CHECK( pde == doctest::Approx( ana ).epsilon( 0.01 ) );
}
