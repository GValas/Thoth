#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// The !termsheet task renders one contract's YAML description as a Markdown
// document in its result block (`termsheet` literal field): header with the
// levels resolved against the as-of spot, the flavour-specific payoff clause,
// the observation schedule, a disclaimer.

namespace
{

//! shared market block + one contract + the termsheet task
std::string TsCfg( const std::string& contract_yaml, const std::string& contract_name )
{
    std::ostringstream o;
    o << "root: ts\n"
      << "ts: !termsheet {today: 2000-01-01, contract: " << contract_name
      << ", result: res}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [3, 3]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 20, calendar: cal}\n"
      << contract_yaml;
    return o.str();
}

std::string Doc( const std::string& cfg )
{
    return Price( cfg )["res"]["termsheet"].as<std::string>();
}

} // namespace

TEST_CASE( "termsheet: vanilla renders strike, style and dates" )
{
    //! relative strike: 110% of the as-of spot must render as cash 110
    std::string doc = Doc( TsCfg(
        "o: !vanilla {underlying: eq, premium_currency: eur, strike: 110,"
        " is_absolute_strike: false, maturity: 2000-12-31, nominal: 1, type: call,"
        " exercise: american}\n",
        "o" ) );
    CHECK( doc.find( "**American call**" ) != std::string::npos );
    CHECK( doc.find( "K = 110" ) != std::string::npos ); //!< resolved against spot 100
    CHECK( doc.find( "2000-12-31" ) != std::string::npos );
    CHECK( doc.find( "## Disclaimer" ) != std::string::npos );
    CHECK( doc.find( "\\max(S_{\\tau} - K" ) != std::string::npos ); //!< LaTeX payoff (American call)
}

TEST_CASE( "termsheet: barrier renders flavour, level and monitoring" )
{
    std::string doc = Doc( TsCfg(
        "o: !barrier {underlying: eq, premium_currency: eur, strike: 100,"
        " maturity: 2000-12-31, type: put, barrier_type: down&in,"
        " barrier_monitoring_type: continuous_monitoring, barrier_down_level: 80,"
        " nominal: 1}\n",
        "o" ) );
    CHECK( doc.find( "down-and-in put" ) != std::string::npos );
    CHECK( doc.find( "80" ) != std::string::npos );
    CHECK( doc.find( "activated" ) != std::string::npos );
    CHECK( doc.find( "\\mathbf{1}" ) != std::string::npos ); //!< barrier-event indicator formula
}

TEST_CASE( "termsheet: seasoned variance swap mentions the in-life window" )
{
    std::string doc = Doc( TsCfg(
        "fix: !simple_fixing_data {underlying: eq,"
        " dates: [1999-07-05, 1999-08-04, 1999-09-03, 1999-10-03, 1999-11-02, 1999-12-02],"
        " values: [100, 92, 103, 96, 108, 99]}\n"
        "o: !variance {underlying: eq, premium_currency: eur, maturity: 2000-12-31,"
        " volatility_strike: 25, notional: 10000, observation_period_days: 30,"
        " start: 1999-07-05, fixings: fix}\n",
        "o" ) );
    CHECK( doc.find( "25%" ) != std::string::npos );
    CHECK( doc.find( "in-life (seasoned)" ) != std::string::npos );
    CHECK( doc.find( "## Observation schedule" ) != std::string::npos );
    CHECK( doc.find( "\\sigma^2_{\\mathrm{real}}" ) != std::string::npos ); //!< estimator formula
}

TEST_CASE( "termsheet: Phoenix autocallable renders the schedule and the memory" )
{
    std::string doc = Doc( TsCfg(
        "o: !autocallable {underlying: eq, premium_currency: eur, maturity: 2001-12-31,"
        " autocall_dates: [2000-06-30, 2000-12-29, 2001-06-29], autocall_barrier: 100,"
        " protection_barrier: 60, coupon: 2, coupon_barrier: 70, coupon_memory: true,"
        " nominal: 100}\n",
        "o" ) );
    CHECK( doc.find( "memory feature" ) != std::string::npos );
    CHECK( doc.find( "coupon barrier of **70**" ) != std::string::npos );
    CHECK( doc.find( "| 3 | 2001-06-29 |" ) != std::string::npos ); //!< schedule table
    CHECK( doc.find( "protection barrier of **60**" ) != std::string::npos );
    CHECK( doc.find( "\\begin{cases}" ) != std::string::npos ); //!< piecewise redemption
    CHECK( doc.find( "(1 + m_k)" ) != std::string::npos );      //!< memory multiplier
}

TEST_CASE( "termsheet: Asian and ratchet render their formal payoff" )
{
    std::string asian = Doc( TsCfg(
        "o: !asian {underlying: eq, premium_currency: eur, strike: 100,"
        " is_absolute_strike: true, maturity: 2000-12-31, type: call, nominal: 1,"
        " observation_period_days: 90}\n",
        "o" ) );
    CHECK( asian.find( "average-price Asian call" ) != std::string::npos );
    CHECK( asian.find( "\\frac{1}{" ) != std::string::npos ); //!< the average formula
    CHECK( asian.find( "Averaging fixings" ) != std::string::npos );

    std::string ratchet = Doc( TsCfg(
        "o: !ratchet {underlying: eq, premium_currency: eur, maturity: 2000-12-31,"
        " nominal: 100, observation_period_days: 90, local_floor: -5, local_cap: 5,"
        " global_floor: 0}\n",
        "o" ) );
    CHECK( ratchet.find( "ratchet (cliquet) note" ) != std::string::npos );
    CHECK( ratchet.find( "locked in" ) != std::string::npos );
    CHECK( ratchet.find( "R_i = S_{t_i}/S_{t_{i-1}}" ) != std::string::npos );
    CHECK( ratchet.find( "Period boundaries" ) != std::string::npos );
}

TEST_CASE( "termsheet: a missing contract reference is rejected" )
{
    std::string cfg = TsCfg( "o: !vanilla {underlying: eq, premium_currency: eur,"
                             " strike: 100, maturity: 2000-12-31, type: call,"
                             " exercise: european}\n",
                             "o" );
    const std::string from = "contract: o,", to = "contract: missing,";
    cfg.replace( cfg.find( from ), from.size(), to );
    CHECK_THROWS_AS( Price( cfg ), std::runtime_error );
}
