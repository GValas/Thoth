#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Bump-and-revalue Greeks (Pricer::BumpAndRevalueGreeks) for a European vanilla,
// checked against the closed-form Black-Scholes Greeks. The ANA pricer is used so
// the base prices are exact BS (no MC noise, no PDE grid error): the only error is
// the finite-difference truncation of the bump engine itself. This pins the
// delta/gamma/vega/rho/theta engine that the book- and contract-level Greeks share.

namespace
{
//! a single European vanilla on one equity, requesting the full Greek set.
std::string GreeksCfg( double spot, double strike, double vol_pct, double rate_pct,
                       const std::string& type, const std::string& method )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor,"
      << " indicators: [premium, delta, gamma, vega, rho, theta], result: res}\n"
      << CfgBlock( 1, 30, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << rate_pct << ", " << rate_pct << "]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: " << spot << ", volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: " << vol_pct << ", calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: " << strike
      << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: european}\n";
    return o.str();
}
} // namespace

TEST_CASE( "ANA bump Greeks match Black-Scholes (delta/gamma/vega/rho/theta)" )
{
    const double S = 100, r = 0.08, vol = 0.30;

    for ( double K : { 90.0, 100.0, 115.0 } )
    {
        for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
        {
            CAPTURE( K );
            CAPTURE( type );
            auto res = Price( GreeksCfg( S, K, vol * 100, r * 100, type, "ana" ) );

            const bool call = ( type == "call" );

            //! delta : one-sided 1% spot bump -> O(h) truncation ~ 0.5*gamma*h
            const double dref = call ? BsCallDelta( S, K, r, vol, T1 )
                                     : BsPutDelta( S, K, r, vol, T1 );
            CHECK( std::abs( Greek( res, "delta" ) - dref ) <= 0.015 );

            //! gamma : central 10% spot bump (wide, so the curvature clears grid
            //! noise on the PDE) -> looser relative tolerance
            const double gref = BsGamma( S, K, r, vol, T1 );
            CHECK( std::abs( Greek( res, "gamma" ) - gref ) <= 0.15 * gref + 1e-4 );

            //! vega : engine reports per 1 vol point (BS vega per unit * 0.01)
            const double vref = BsVega( S, K, r, vol, T1 ) * 0.01;
            CHECK( std::abs( Greek( res, "vega" ) - vref ) <= 0.02 );

            //! rho : engine reports per 1% rate move (BS rho per unit * 0.01)
            const double rref = ( call ? BsCallRho( S, K, r, vol, T1 )
                                       : BsPutRho( S, K, r, vol, T1 ) ) *
                                0.01;
            CHECK( std::abs( Greek( res, "rho" ) - rref ) <= 0.01 );

            //! theta : engine rolls one calendar day (ACT/365) and reprices, so the
            //! reference is the exact BS reprice at T = 364/365 minus at T = 1
            const double base = call ? BsCall( S, K, r, vol, T1 ) : BsPut( S, K, r, vol, T1 );
            const double rolled = call ? BsCall( S, K, r, vol, 364.0 / 365.0 )
                                       : BsPut( S, K, r, vol, 364.0 / 365.0 );
            CHECK( std::abs( Greek( res, "theta" ) - ( rolled - base ) ) <= 1e-3 );
        }
    }
}

// The PDE engine drives the same bump-and-revalue Greeks, but its base prices
// carry grid discretisation error, so its spot Greeks are held to a looser band.
TEST_CASE( "PDE bump delta/gamma are close to Black-Scholes" )
{
    const double S = 100, K = 100, r = 0.08, vol = 0.30;
    auto res = Price( GreeksCfg( S, K, vol * 100, r * 100, "call", "pde" ) );

    CHECK( std::abs( Greek( res, "delta" ) - BsCallDelta( S, K, r, vol, T1 ) ) <= 0.03 );
    CHECK( std::abs( Greek( res, "gamma" ) - BsGamma( S, K, r, vol, T1 ) ) <= 0.2 * BsGamma( S, K, r, vol, T1 ) + 1e-3 );
}
