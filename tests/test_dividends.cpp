#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;
using doctest::Approx;

// Continuous dividends enter the engine forward (F = S e^{(r-q)T}); repo does
// not (it only shifts local vol, which is flat here). Only the analytic engine
// carries q correctly, so these checks use method: ana.

namespace
{
//! single european vanilla on a dividend-paying equity, priced analytically.
std::string DivCfg( double spot, double strike, double vol_pct, double rate_pct,
                    double div_pct, const std::string& type )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( "ana", 1, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << rate_pct << ", " << rate_pct << "]}\n"
      << "div: !continuous_dividends_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << div_pct << ", " << div_pct << "]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: " << spot
      << ", volatility: vol, currency: eur, continuous_dividends: div}\n"
      << "vol: !bs_volatility {volatility: " << vol_pct << ", calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: " << strike
      << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: european}\n";
    return o.str();
}

//! carry-adjusted Black-Scholes: F = S e^{(r-q)T}, discounted at r.
double BsCarry( double S, double K, double r, double q, double sig, double T,
                const std::string& type )
{
    double F = S * std::exp( ( r - q ) * T );
    double df = std::exp( -r * T );
    double d1 = ( std::log( F / K ) + 0.5 * sig * sig * T ) / ( sig * std::sqrt( T ) );
    double d2 = d1 - sig * std::sqrt( T );
    double c = df * ( F * NormCdf( d1 ) - K * NormCdf( d2 ) );
    return type == "call" ? c : c - df * ( F - K ); //!< put-call parity
}
} // namespace

TEST_CASE( "continuous dividends are carried in the forward (analytic)" )
{
    const double S = 100, K = 100, r = 0.08, q = 0.04, vol = 0.30;

    double call = Premium( Price( DivCfg( S, K, vol * 100, r * 100, q * 100, "call" ) ) );
    double put = Premium( Price( DivCfg( S, K, vol * 100, r * 100, q * 100, "put" ) ) );

    CHECK( call == Approx( BsCarry( S, K, r, q, vol, T1, "call" ) ).epsilon( 1e-6 ) );
    CHECK( put == Approx( BsCarry( S, K, r, q, vol, T1, "put" ) ).epsilon( 1e-6 ) );

    // a positive dividend makes the call cheaper and the put dearer than no-div
    CHECK( call < BsCall( S, K, r, vol, T1 ) );
    CHECK( put > BsPut( S, K, r, vol, T1 ) );
}

TEST_CASE( "zero dividend reproduces plain Black-Scholes" )
{
    const double S = 100, K = 110, r = 0.06, vol = 0.25;
    double call = Premium( Price( DivCfg( S, K, vol * 100, r * 100, 0, "call" ) ) );
    CHECK( call == Approx( BsCall( S, K, r, vol, T1 ) ).epsilon( 1e-6 ) );
}
