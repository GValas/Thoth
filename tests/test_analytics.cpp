#include "finance.hpp"
#include "helpers.hpp"
#include <doctest/doctest.h>

using doctest::Approx;

TEST_CASE( "payoff_vanilla call/put intrinsic" )
{
    // payoff_vanilla(spot, strike, type, has_cap, cap, has_floor, floor)
    CHECK( payoff_vanilla( 120, 100, OptionType::Call, false, 0, true, 0 ) == Approx( 20 ) );
    CHECK( payoff_vanilla( 80, 100, OptionType::Call, false, 0, true, 0 ) == Approx( 0 ) ); // OTM floored at 0
    CHECK( payoff_vanilla( 80, 100, OptionType::Put, false, 0, true, 0 ) == Approx( 20 ) );
    CHECK( payoff_vanilla( 120, 100, OptionType::Put, false, 0, true, 0 ) == Approx( 0 ) );
    CHECK( payoff_vanilla( 100, 100, OptionType::Call, false, 0, true, 0 ) == Approx( 0 ) ); // ATM
}

TEST_CASE( "BS_Call_Price matches the analytic reference" )
{
    struct Case
    {
        double S, K, r, vol, T;
    };
    for ( Case c : { Case{ 100, 100, 0.08, 0.30, 1.0 },
                     Case{ 120, 100, 0.05, 0.20, 0.5 },
                     Case{ 90, 110, 0.03, 0.40, 2.0 },
                     Case{ 100, 80, 0.08, 0.25, 1.0 } } )
    {
        double fwd = c.S * std::exp( c.r * c.T );
        double df = std::exp( -c.r * c.T );
        CHECK( BS_Call_Price( fwd, c.K, c.T, c.vol, df ) == Approx( test::BsCall( c.S, c.K, c.r, c.vol, c.T ) ).epsilon( 1e-9 ) );
        CHECK( BS_Put_Price( fwd, c.K, c.T, c.vol, df ) == Approx( test::BsPut( c.S, c.K, c.r, c.vol, c.T ) ).epsilon( 1e-9 ) );
    }
}

TEST_CASE( "put-call parity holds for the BS formulas" )
{
    double S = 100, K = 105, r = 0.08, vol = 0.30, T = 1.0;
    double fwd = S * std::exp( r * T ), df = std::exp( -r * T );
    double call = BS_Call_Price( fwd, K, T, vol, df );
    double put = BS_Put_Price( fwd, K, T, vol, df );
    // C - P = S - K e^{-rT}
    CHECK( ( call - put ) == Approx( S - K * df ).epsilon( 1e-9 ) );
}

TEST_CASE( "known Black-Scholes value (sanity anchor)" )
{
    // ATM 1y call, spot 100, vol 30%, r 8% -> 15.7113 (classic)
    CHECK( test::BsCall( 100, 100, 0.08, 0.30, 1.0 ) == Approx( 15.7113 ).epsilon( 1e-4 ) );
    CHECK( test::BsPut( 100, 100, 0.08, 0.30, 1.0 ) == Approx( 8.0226 ).epsilon( 1e-4 ) );
}

// End-to-end: the "ana" method drives the closed-form book pricer
// (PricerANA), which must reproduce Black-Scholes for european vanillas.
TEST_CASE( "ANA book pricing reproduces Black-Scholes (closed form)" )
{
    using namespace test;
    const double S = 100, r = 0.08;

    for ( double K : { 80.0, 100.0, 120.0 } )
    {
        for ( double vol : { 20.0, 30.0, 40.0 } )
        {
            for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
            {
                CAPTURE( K );
                CAPTURE( vol );
                CAPTURE( type );

                auto res = Price( VanillaCfg( S, K, vol, r * 100, type, "european",
                                              /*draws=*/1, /*pde=*/false, /*precision=*/5,
                                              /*method=*/"ana" ) );
                double ana = Premium( res );
                double bs = ( type == "call" )
                                ? BsCall( S, K, r, vol / 100, T1 )
                                : BsPut( S, K, r, vol / 100, T1 );
                // closed form vs closed form: convention-level agreement only
                CHECK( std::abs( ana - bs ) <= 1e-2 );
            }
        }
    }
}
