#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Book aggregation (Pricer::AggregateContract): the book premium is the sum of its
// contracts' premiums (FX-converted to the book currency; here a single currency,
// so the factor is 1). This is the same accumulation the cluster master performs
// when it recombines slave results — the distributed master/slave path-split runs
// over HTTP and is exercised separately, but the per-contract -> book aggregation
// it relies on is pinned here in-process for both the deterministic (ANA) and the
// Monte-Carlo (MCL) engines.

namespace
{
//! a two-option book on one equity (two strikes), priced together.
std::string TwoOptionBookCfg( const std::string& method, int draws )
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
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "book: !book {contracts: [o1, o2]}\n"
      << "o1: !vanilla {underlying: eq, premium_currency: eur, strike: 90,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n"
      << "o2: !vanilla {underlying: eq, premium_currency: eur, strike: 110,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: put, exercise: european}\n";
    return o.str();
}
} // namespace

TEST_CASE( "book premium is the sum of its contracts (ANA, deterministic)" )
{
    auto res = Price( TwoOptionBookCfg( "ana", 1 ) );
    const double sum = Premium( res, "o1" ) + Premium( res, "o2" );
    CHECK( Premium( res ) == doctest::Approx( sum ).epsilon( 1e-9 ) );
}

TEST_CASE( "book premium is the sum of its contracts (MCL)" )
{
    auto res = Price( TwoOptionBookCfg( "mcl", 50000 ) );
    const double sum = Premium( res, "o1" ) + Premium( res, "o2" );
    //! same diffusion, so the book total is the exact sum of the per-contract legs
    CHECK( Premium( res ) == doctest::Approx( sum ).epsilon( 1e-9 ) );
}
