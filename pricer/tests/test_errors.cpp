#include "yaml_config.hpp"
#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// The engine should fail loudly (std::runtime_error) on inconsistent input.

TEST_CASE( "unknown pricer kind is rejected" )
{
    // "quantum" yields the tag !quantum_pricer, which no registry factory claims
    std::string y = VanillaCfg( 100, 100, 30, 5, "call", "european", 1, false, 5, "quantum" );
    CHECK_THROWS_AS( Price( y ), std::runtime_error );
}

TEST_CASE( "MCL without a correlation matrix throws" )
{
    // a book priced with mcl but no `correlation:` field on the pricer
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " mcl_configuration: cfg_mcl, indicators: [premium], result: res}\n"
      << CfgBlock( 100, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "book: !book {contracts: [o]}\n"
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
      << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( 1, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "book: !book {contracts: [ghost]}\n";
    CHECK_THROWS_AS( Price( o.str() ), std::runtime_error );
}

TEST_CASE( "a non positive-definite correlation matrix is rejected" )
{
    // off-diagonal 1.5 (>1) makes the 2x2 correlation matrix indefinite; the
    // correlation object must reject it at load rather than misprice silently.
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( 1, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq2], matrix: [1, 1.5, 1.5, 1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "eq2: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " maturity: 2000-12-31, type: call, exercise: european}\n";
    CHECK_THROWS_AS( Price( o.str() ), std::runtime_error );
}

TEST_CASE( "YamlConfig getter without default throws on a missing key" )
{
    YamlConfig cfg( YamlConfig::from_string_t{}, "node: {a: 1}\n" );
    CHECK_THROWS_AS( cfg.GetDouble( "node.missing" ), std::runtime_error );
    CHECK( cfg.GetDouble( "node.missing", -1.0 ) == -1.0 ); //!< default overload is safe
}

//! --- security / resource caps on YAML-driven sizes (constants.hpp) ---

TEST_CASE( "PDE grid sizes are bounded and positive" )
{
    //! an absurd custom_n_s must be rejected, not allocated
    std::string big = VanillaCfg( 100, 100, 30, 5, "call", "european", 1, true, 5 );
    const std::string from = "vanilla_precision: high";
    std::string huge = big;
    huge.replace( huge.find( from ), from.size(),
                  "custom_n_s: 2000000000, custom_n_t: 500, custom_sigma_factor: 5" );
    CHECK_THROWS_AS( Price( huge ), std::runtime_error );

    //! a negative grid size (would cast to a huge size_t) is rejected too
    std::string neg = big;
    neg.replace( neg.find( from ), from.size(),
                 "custom_n_s: -1, custom_n_t: 500, custom_sigma_factor: 5" );
    CHECK_THROWS_AS( Price( neg ), std::runtime_error );
}

TEST_CASE( "MCL path count is bounded" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " mcl_configuration: m, correlation: cor, indicators: [premium], result: res}\n"
      << "m: !mcl_configuration {max_day_step: 30, min_day_step: -1, paths: 999999999999,"
      << " vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " maturity: 2000-12-31, type: call, exercise: european}\n";
    CHECK_THROWS_AS( Price( o.str() ), std::runtime_error );
}

TEST_CASE( "a deep object-reference chain is rejected (stack-overflow guard)" )
{
    //! a linear chain of chained yield curves longer than the depth cap: build a
    //! book whose currency's rate references curve_0 -> curve_1 -> ... via a
    //! (contrived) self-referential structure is not expressible with real kinds,
    //! so instead assert the guard exists by a very long !sequence-of-sequences.
    std::ostringstream o;
    o << "root: s0\n";
    const int depth = 1200; //!< above MAX_OBJECT_REFERENCE_DEPTH (512)
    for ( int i = 0; i < depth; i++ )
    {
        o << "s" << i << ": !sequence {result: r" << i << ", tasks: [s" << ( i + 1 ) << "]}\n";
    }
    //! terminal task so the chain is well-formed up to the leaf
    o << "s" << depth << ": !sequence {result: r" << depth << ", tasks: []}\n";
    CHECK_THROWS_AS( Price( o.str(), "s0" ), std::runtime_error );
}
