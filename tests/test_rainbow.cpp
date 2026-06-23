#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Best-of / worst-of (rainbow) baskets, priced by the MCL engine on a max/min of
// the members' rebased performances. Two EUR equities (spot 100), 8% rate.
namespace
{
//! a one-contract rainbow book. type = best_of | worst_of, vols/correl/strike
//! parameterised; the option is a call on the rainbow underlying.
std::string RainbowCfg( const std::string& type, double strike,
                        double vol1, double vol2, double rho, int draws = 200000 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur, mcl_configuration: m,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "m: !mcl_configuration {max_day_step: 7, min_day_step: -1, paths: " << draws
      << ", vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq1: !equity {spot: 100, volatility: vol1, currency: eur}\n"
      << "eq2: !equity {spot: 100, volatility: vol2, currency: eur}\n"
      << "vol1: !bs_volatility {volatility: " << vol1 << ", calendar: cal}\n"
      << "vol2: !bs_volatility {volatility: " << vol2 << ", calendar: cal}\n"
      << "rb: !rainbow {underlyings: [eq1, eq2], type: " << type << "}\n"
      << "cor: !correlation_matrix {underlyings: [eq1, eq2], matrix: [1, " << rho
      << ", " << rho << ", 1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: rb, premium_currency: eur, strike: " << strike
      << ", maturity: 2000-12-31, type: call, exercise: european}\n";
    return o.str();
}

//! single-asset vanilla call (for the bound / identity checks), via the same market
std::string SingleCfg( double spot, double vol, double strike )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur, mcl_configuration: m,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "m: !mcl_configuration {max_day_step: 7, min_day_step: -1, paths: 200000,"
      << " vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: " << spot << ", volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: " << vol << ", calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: " << strike
      << ", maturity: 2000-12-31, type: call, exercise: european}\n";
    return o.str();
}
} // namespace

// Strike-0 best-of / worst-of forwards have an exact closed form (Margrabe), so
// the MCL max/min simulation can be checked against it. Rebased performances have
// forward 100*e^{rT}; the rainbow forward is e^{-rT}*E[max or min].
TEST_CASE( "rainbow best-of / worst-of forwards match the closed form" )
{
    const double r = 0.08, v1 = 0.30, v2 = 0.25, rho = 0.5;
    const double F = 100.0 * std::exp( r * T1 ); //!< forward of each rebased performance
    const double df = std::exp( -r * T1 );

    const double best_ref = df * ExpMaxLN( F, F, v1, v2, rho, T1 );
    const double worst_ref = df * ExpMinLN( F, F, v1, v2, rho, T1 );

    auto best = Price( RainbowCfg( "best_of", 0, 30, 25, rho ) );
    auto worst = Price( RainbowCfg( "worst_of", 0, 30, 25, rho ) );
    CAPTURE( best_ref );
    CAPTURE( worst_ref );
    CHECK( std::abs( Premium( best ) - best_ref ) <= 6.0 * Trust( best ) + 0.1 );
    CHECK( std::abs( Premium( worst ) - worst_ref ) <= 6.0 * Trust( worst ) + 0.1 );
    CHECK( Premium( best ) > Premium( worst ) ); //!< best-of dominates worst-of
}

// Payoff identity: for any two values, max and min are just the sorted pair, so
// (max-K)^+ + (min-K)^+ = (S1-K)^+ + (S2-K)^+. Hence best-of-call + worst-of-call
// equals the sum of the two single-asset calls (same shared market / seed).
TEST_CASE( "rainbow best-of + worst-of call equals the sum of single-asset calls" )
{
    const double K = 100, v1 = 30, v2 = 25, rho = 0.5;
    const double best = Premium( Price( RainbowCfg( "best_of", K, v1, v2, rho ) ) );
    const double worst = Premium( Price( RainbowCfg( "worst_of", K, v1, v2, rho ) ) );
    const double c1 = Premium( Price( SingleCfg( 100, v1, K ) ) );
    const double c2 = Premium( Price( SingleCfg( 100, v2, K ) ) );
    CAPTURE( best );
    CAPTURE( worst );
    CAPTURE( c1 );
    CAPTURE( c2 );
    CHECK( std::abs( ( best + worst ) - ( c1 + c2 ) ) <= 0.15 );

    // bounds : worst-of <= each single <= best-of
    CHECK( worst <= c1 + 0.1 );
    CHECK( worst <= c2 + 0.1 );
    CHECK( best >= c1 - 0.1 );
    CHECK( best >= c2 - 0.1 );
}

// Two identical assets : best-of dominates and worst-of is dominated by the
// single-asset option, and the best+worst identity collapses to twice the single
// (since the two single-asset calls are equal). The best-worst spread reflects
// the residual decorrelation and only vanishes as rho -> 1.
TEST_CASE( "rainbow ordering and identity for two identical assets" )
{
    const double K = 100, v = 30, rho = 0.5;
    const double best = Premium( Price( RainbowCfg( "best_of", K, v, v, rho ) ) );
    const double worst = Premium( Price( RainbowCfg( "worst_of", K, v, v, rho ) ) );
    const double single = Premium( Price( SingleCfg( 100, v, K ) ) );
    CAPTURE( best );
    CAPTURE( worst );
    CAPTURE( single );
    CHECK( best >= single - 0.1 );                             //!< best-of dominates
    CHECK( worst <= single + 0.1 );                            //!< worst-of dominated
    CHECK( std::abs( ( best + worst ) - 2 * single ) <= 0.2 ); //!< identity (identical assets)
}
