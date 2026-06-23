#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// The engine seeds its RNG deterministically and walks the node graph in a
// name-ordered fashion, so the same config must yield bit-identical results.
TEST_CASE( "single-asset MC pricing is bit-reproducible" )
{
    std::string cfg = VanillaCfg( 100, 105, 30, 8, "call", "european", 100000 );
    auto a = Price( cfg );
    auto b = Price( cfg );
    CHECK( Premium( a ) == Premium( b ) );
    CHECK( Trust( a ) == Trust( b ) );
}

// Determinism must also hold for a correlated multi-asset book, where the
// ordering of underlyings / Brownian factors could otherwise leak in.
TEST_CASE( "multi-asset correlated book is bit-reproducible" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " mcl_configuration: cfg_mcl, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( 80000, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq_a, eq_b],"
      << " matrix: [1, 0.4, 0.4, 1]}\n"
      << "eq_a: !equity {spot: 100, volatility: va, currency: eur}\n"
      << "va: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "eq_b: !equity {spot: 90, volatility: vb, currency: eur}\n"
      << "vb: !bs_volatility {volatility: 25, calendar: cal}\n"
      << "o_a: !vanilla {underlying: eq_a, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n"
      << "o_b: !vanilla {underlying: eq_b, premium_currency: eur, strike: 90,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: put, exercise: european}\n"
      << "book: !book {contracts: [o_a, o_b]}\n";
    std::string cfg = o.str();

    auto a = Price( cfg );
    auto b = Price( cfg );
    CHECK( Premium( a ) == Premium( b ) );
    CHECK( Premium( a, "o_a" ) == Premium( b, "o_a" ) );
    CHECK( Premium( a, "o_b" ) == Premium( b, "o_b" ) );
}
