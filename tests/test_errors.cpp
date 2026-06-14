#include "yaml_config.hpp"
#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// The engine should fail loudly (std::runtime_error) on inconsistent input.

TEST_CASE( "unknown method is rejected" )
{
    std::string y = VanillaCfg( 100, 100, 30, 5, "call", "european", 1, false, 5, "quantum" );
    CHECK_THROWS_AS( Price( y ), std::runtime_error );
}

TEST_CASE( "MCL without a correlation matrix throws" )
{
    // a book priced with mcl but no `correlation:` field on the pricer
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, indicators: [premium], result: res}\n"
      << CfgBlock( "mcl", 100, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    CHECK_THROWS_AS( Price( o.str() ), std::runtime_error );
}

TEST_CASE( "an unpriceable barrier (unknown type) is rejected" )
{
    std::string y = BarrierCfg( 100, 100, 120, 30, 5, "call", "sideways_and_out", "ana" );
    CHECK_THROWS_AS( Price( y ), std::runtime_error );
}

TEST_CASE( "a missing referenced object is rejected" )
{
    // the book references a contract `ghost` that is never defined
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( "ana", 1, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "book: !book {options: [ghost]}\n";
    CHECK_THROWS_AS( Price( o.str() ), std::runtime_error );
}

TEST_CASE( "YamlConfig getter without default throws on a missing key" )
{
    YamlConfig cfg( YamlConfig::from_string_t{}, "node: {a: 1}\n" );
    CHECK_THROWS_AS( cfg.GetDouble( "node.missing" ), std::runtime_error );
    CHECK( cfg.GetDouble( "node.missing", -1.0 ) == -1.0 ); //!< default overload is safe
}
