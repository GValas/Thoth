#include "finance.hpp"
#include <cmath>
#include <doctest/doctest.h>

using doctest::Approx;

// --- payoffs ---------------------------------------------------------------

TEST_CASE( "payoff_vanilla: intrinsic, cap and floor" )
{
    // call / put intrinsic, floored at 0
    CHECK( payoff_vanilla( 120, 100, OptionType::Call, false, 0, true, 0 ) == Approx( 20 ) );
    CHECK( payoff_vanilla( 80, 100, OptionType::Call, false, 0, true, 0 ) == Approx( 0 ) );
    CHECK( payoff_vanilla( 80, 100, OptionType::Put, false, 0, true, 0 ) == Approx( 20 ) );
    // a cap truncates a large payoff
    CHECK( payoff_vanilla( 200, 100, OptionType::Call, true, 30, true, 0 ) == Approx( 30 ) );
    // a non-zero floor lifts an out-of-the-money payoff
    CHECK( payoff_vanilla( 80, 100, OptionType::Call, false, 0, true, 5 ) == Approx( 5 ) );
}

TEST_CASE( "an unknown option type is rejected at parse time" )
{
    CHECK_THROWS_AS( ParseOptionType( "straddle" ), std::runtime_error );
    CHECK( ParseOptionType( "call" ) == OptionType::Call );
    CHECK( ParseOptionType( "put" ) == OptionType::Put );
}

TEST_CASE( "payoff_digital: the four barrier types" )
{
    // up&out : 0 at/above the up level, 1 below
    CHECK( payoff_digital( 120, "up&out", 110, 90 ) == Approx( 0 ) );
    CHECK( payoff_digital( 100, "up&out", 110, 90 ) == Approx( 1 ) );
    // up&in : the complement
    CHECK( payoff_digital( 120, "up&in", 110, 90 ) == Approx( 1 ) );
    CHECK( payoff_digital( 100, "up&in", 110, 90 ) == Approx( 0 ) );
    // down&out : 0 at/below the down level, 1 above
    CHECK( payoff_digital( 80, "down&out", 110, 90 ) == Approx( 0 ) );
    CHECK( payoff_digital( 100, "down&out", 110, 90 ) == Approx( 1 ) );
    // down&in : the complement
    CHECK( payoff_digital( 80, "down&in", 110, 90 ) == Approx( 1 ) );
    CHECK( payoff_digital( 100, "down&in", 110, 90 ) == Approx( 0 ) );
}

TEST_CASE( "payoff_digital rejects an unknown barrier type" )
{
    CHECK_THROWS_AS( payoff_digital( 100, "sideways", 110, 90 ), std::runtime_error );
}

// --- Black-Scholes ---------------------------------------------------------

TEST_CASE( "BS put-call parity at the formula level" )
{
    double F = 105, K = 100, T = 0.75, vol = 0.30, df = std::exp( -0.05 * T );
    double c = BS_Call_Price( F, K, T, vol, df );
    double p = BS_Put_Price( F, K, T, vol, df );
    CHECK( ( c - p ) == Approx( df * ( F - K ) ).epsilon( 1e-12 ) );
}

TEST_CASE( "BS degenerate inputs collapse to discounted intrinsic" )
{
    double F = 120, K = 100, df = 0.95;
    CHECK( BS_Call_Price( F, K, 1.0, 0.0, df ) == Approx( df * ( F - K ) ) ); //!< zero vol
    CHECK( BS_Call_Price( F, K, 0.0, 0.3, df ) == Approx( df * ( F - K ) ) ); //!< zero maturity
    CHECK( BS_Vega( F, K, 1.0, 0.0, df ) == Approx( 0 ) );
    CHECK( BS_Gamma( F, K, 0.0, 0.3, df ) == Approx( 0 ) );
}

TEST_CASE( "implied vol inverts the call price" )
{
    double F = 100, K = 95, T = 1.0, df = std::exp( -0.05 );
    for ( double vol : { 0.10, 0.20, 0.35, 0.60 } )
    {
        CAPTURE( vol );
        double price = BS_Call_Price( F, K, T, vol, df );
        double iv = BS_Call_ImplicitVol( F, K, T, price, df );
        CHECK( iv == Approx( vol ).epsilon( 1e-4 ) );
    }
}

TEST_CASE( "implied vol: safeguarded Newton handles the extremes" )
{
    const double F = 100, df = std::exp( -0.05 );

    //! deep OTM / deep ITM / short-dated / very high vol: the raw Newton step from a
    //! 30% warm start overshoots or stalls on the flat vega here; the bisection
    //! safeguard must still round-trip price -> vol -> price.
    struct Case
    {
        double K, T, vol;
    };
    //! (deeper ITM than K=40 is ill-posed by nature: the price stops encoding the
    //! vol to within the solver tolerance, so no inversion can recover it)
    for ( const Case& c : { Case{ 300, 1.0, 0.20 },    //!< deep OTM, tiny vega
                            Case{ 40, 1.0, 0.40 },     //!< deep ITM, price near its bounds
                            Case{ 100, 0.01, 0.15 },   //!< short-dated
                            Case{ 150, 0.5, 1.50 } } ) //!< very high target vol
    {
        CAPTURE( c.K );
        CAPTURE( c.T );
        CAPTURE( c.vol );
        const double price = BS_Call_Price( F, c.K, c.T, c.vol, df );
        const double iv = BS_Call_ImplicitVol( F, c.K, c.T, price, df );
        CHECK( iv == Approx( c.vol ).epsilon( 1e-3 ) );
    }

    //! a price outside the no-arbitrage bounds has NO implied vol: fail loudly
    CHECK_THROWS( BS_Call_ImplicitVol( F, 95, 1.0, df * F * 1.01, df ) ); //!< above df*F
    CHECK_THROWS( BS_Call_ImplicitVol( F, 95, 1.0, df * 5 * 0.99, df ) ); //!< below intrinsic
}

TEST_CASE( "BS greek formulas are mutually consistent (finite differences)" )
{
    double F = 100, K = 100, T = 1.0, vol = 0.30, df = std::exp( -0.05 );

    // delta = d Price / d Forward
    double h = 1e-4;
    double fd_delta = ( BS_Call_Price( F + h, K, T, vol, df ) - BS_Call_Price( F - h, K, T, vol, df ) ) / ( 2 * h );
    CHECK( BS_Call_Delta( F, K, T, vol, df ) == Approx( fd_delta ).epsilon( 1e-5 ) );

    // gamma = d Delta / d Forward
    double fd_gamma = ( BS_Call_Delta( F + h, K, T, vol, df ) - BS_Call_Delta( F - h, K, T, vol, df ) ) / ( 2 * h );
    CHECK( BS_Gamma( F, K, T, vol, df ) == Approx( fd_gamma ).epsilon( 1e-4 ) );

    // vega = d Price / d Vol
    double hv = 1e-5;
    double fd_vega = ( BS_Call_Price( F, K, T, vol + hv, df ) - BS_Call_Price( F, K, T, vol - hv, df ) ) / ( 2 * hv );
    CHECK( BS_Vega( F, K, T, vol, df ) == Approx( fd_vega ).epsilon( 1e-4 ) );

    // engine convention: put delta = call delta - 1
    CHECK( BS_Put_Delta( F, K, T, vol, df ) == Approx( BS_Call_Delta( F, K, T, vol, df ) - 1 ) );
}
