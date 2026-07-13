#include "helpers.hpp"
#include <doctest/doctest.h>
#include <vector>

using namespace test;

// Seasoned (in-life) variance swap: an observation `start` in the past plus a
// !simple_fixing_data with the realised observations. Every engine time-weights
// the realised leg with the fair future leg:
//   fair_total = ( past_sum2 + fair_future * T_future ) / T_total,
// past_sum2 the squared log-returns over the fixings closed by the
// last-fixing -> spot bridge. Start = today - k*period keeps the remaining
// schedule identical to a fresh swap's, so the decomposition is testable exactly.

namespace
{

//! start 6 x 30d before today (2000-01-01), maturity 1y, 30d observations. The
//! past path realises a high vol (deliberately different from the 20% future
//! implied vol so the time-weighting is visible).
const char* START = "1999-07-05"; // 2000-01-01 - 180d
const std::vector<const char*> PAST_DATES = { "1999-07-05", "1999-08-04", "1999-09-03",
                                              "1999-10-03", "1999-11-02", "1999-12-02" };
const std::vector<double> PAST_VALUES = { 100, 92, 103, 96, 108, 99 };

std::string SeasonedCfg( const std::string& method, int draws = 1,
                         bool seasoned = true, const std::string& mutate = "" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( draws, 30, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 20, calendar: cal}\n"
      << "fix: !simple_fixing_data {underlying: eq, dates: [";
    for ( size_t i = 0; i < PAST_DATES.size(); i++ )
    {
        o << ( i ? ", " : "" ) << PAST_DATES[i];
    }
    o << "], values: [";
    for ( size_t i = 0; i < PAST_VALUES.size(); i++ )
    {
        o << ( i ? ", " : "" ) << PAST_VALUES[i];
    }
    o << "]}\n"
      << "book: !book {contracts: [vs]}\n"
      << "vs: !variance {underlying: eq, premium_currency: eur,"
      << " maturity: 2000-12-31, volatility_strike: 25, notional: 10000,"
      << " observation_period_days: 30";
    if ( seasoned )
    {
        o << ", start: " << START << ", fixings: fix";
    }
    o << "}\n";
    std::string s = o.str();
    if ( !mutate.empty() )
    {
        //! mutate is "from|to": apply one textual replacement to the config
        const size_t bar = mutate.find( '|' );
        const std::string from = mutate.substr( 0, bar ), to = mutate.substr( bar + 1 );
        s.replace( s.find( from ), from.size(), to );
    }
    return s;
}

//! test-side realised leg: squared log-returns over the fixings + spot bridge
double PastSum2( double spot )
{
    double sum2 = 0;
    for ( size_t i = 1; i < PAST_VALUES.size(); i++ )
    {
        const double r = std::log( PAST_VALUES[i] / PAST_VALUES[i - 1] );
        sum2 += r * r;
    }
    const double b = std::log( spot / PAST_VALUES.back() );
    return sum2 + b * b;
}

} // namespace

// Exact decomposition: with start = today - 6*30d the remaining schedule equals
// the fresh swap's (both are anchor + k*30d with aligned anchors), so
//   seasoned = df*N*( (past + fair_fut*T_fut)/T_tot - K^2 )
// where fair_fut is recovered from the FRESH ANA price of the same swap.
TEST_CASE( "seasoned variance swap: ANA equals the past/future time-weighted mix" )
{
    const double df = std::exp( -0.05 * T1 ), notl = 10000, k2 = 0.25 * 0.25;
    const double t_fut = T1;                 //!< today -> maturity
    const double t_tot = T1 + 180.0 / 365.0; //!< start -> maturity (ACT/365)

    const double fresh = Premium( Price( SeasonedCfg( "ana", 1, false ) ) );
    const double fair_fut = fresh / ( df * notl ) + k2; //!< invert the fresh PV

    const double expected =
        df * notl * ( ( PastSum2( 100 ) + fair_fut * t_fut ) / t_tot - k2 );
    const double seasoned = Premium( Price( SeasonedCfg( "ana" ) ) );
    CHECK( seasoned == doctest::Approx( expected ).epsilon( 1e-10 ) );

    //! the realised leg (high realised vol) must pull the seasoned PV above the
    //! fresh one weighted alone — sanity that the past actually contributes
    CHECK( seasoned != doctest::Approx( fresh ).epsilon( 1e-3 ) );
}

// The three engines agree on the seasoned swap (same convention everywhere:
// realised past + bridge, future leg priced from today, total-window annualizer).
TEST_CASE( "seasoned variance swap: ANA, PDE and MCL agree" )
{
    const double ana = Premium( Price( SeasonedCfg( "ana" ) ) );
    const double pde = Premium( Price( SeasonedCfg( "pde" ) ) );
    auto mr = Price( SeasonedCfg( "mcl", 100000 ) );
    const double mcl = Premium( mr );

    CHECK( std::abs( pde - ana ) <= 0.01 * std::abs( ana ) + 1.0 ); //!< grid vs strip
    CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mr ) + 0.01 * std::abs( ana ) );
}

// Theta rolls today forward one day: the schedule date the roll pushes into the
// past has no fixing by construction (the realised path is frozen at valuation),
// so it fixes at the held live spot — pricing must not abort, and the roll
// convention zeroes the bridge over the rolled interval.
TEST_CASE( "seasoned variance swap: theta prices under the roll convention" )
{
    std::string cfg = SeasonedCfg( "ana", 1, true,
                                   "indicators: [premium]|indicators: [premium, theta]" );
    auto r = Price( cfg ); //!< must not throw "missing fixing on <today>"
    CHECK( std::isfinite( Greek( r, "theta" ) ) );
    //! the base premium is unchanged by requesting theta
    CHECK( Premium( r ) == doctest::Approx( Premium( Price( SeasonedCfg( "ana" ) ) ) )
                               .epsilon( 1e-12 ) );
}

// Invalid seasoned inputs must fail loudly.
TEST_CASE( "seasoned variance swap: invalid inputs are rejected" )
{
    //! a missing schedule fixing (drop one date/value pair by shifting a date)
    CHECK_THROWS_AS( Price( SeasonedCfg( "ana", 1, true, "1999-08-04|1999-08-05" ) ),
                     std::runtime_error );
    //! fixings of another underlying
    CHECK_THROWS_AS( Price( SeasonedCfg( "ana", 1, true, "underlying: eq,|underlying: other," ) ),
                     std::runtime_error );
    //! forward start (after today)
    CHECK_THROWS_AS( Price( SeasonedCfg( "ana", 1, true, "start: 1999-07-05|start: 2000-06-01" ) ),
                     std::runtime_error );
    //! fixings without a start date
    CHECK_THROWS_AS( Price( SeasonedCfg( "ana", 1, true,
                                         "start: 1999-07-05, fixings: fix|fixings: fix" ) ),
                     std::runtime_error );
}
