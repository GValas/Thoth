#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// MC premium must be within a few standard errors of Black-Scholes.
static void check_close( double mc, double bs, double trust )
{
    CHECK( std::abs( mc - bs ) <= 6.0 * trust + 1e-3 );
}

TEST_CASE( "European vanilla MC matches Black-Scholes across a grid" )
{
    const double S = 100, r = 0.08;
    const int draws = 50000;

    for ( double K : { 70.0, 85.0, 100.0, 115.0, 130.0 } )
    {
        for ( double vol : { 15.0, 30.0, 45.0 } )
        {
            for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
            {
                CAPTURE( K );
                CAPTURE( vol );
                CAPTURE( type );

                auto res = Price( VanillaCfg( S, K, vol, r * 100, type, "european", draws ) );
                double mc = Premium( res );
                double trust = Trust( res );
                double bs = ( type == "call" )
                                ? BsCall( S, K, r, vol / 100, T1 )
                                : BsPut( S, K, r, vol / 100, T1 );
                check_close( mc, bs, trust );
            }
        }
    }
}

TEST_CASE( "European pricing respects the spot level (single asset)" )
{
    const double r = 0.08, vol = 30;
    for ( double S : { 50.0, 80.0, 120.0, 200.0 } )
    {
        CAPTURE( S );
        // ATM call scales ~linearly with spot
        auto res = Price( VanillaCfg( S, S, vol, r * 100, "call", "european", 50000 ) );
        check_close( Premium( res ), BsCall( S, S, r, vol / 100, T1 ), Trust( res ) );
    }
}

TEST_CASE( "put-call parity from Monte-Carlo" )
{
    const double S = 100, r = 0.08, vol = 30, K = 105;
    auto c = Price( VanillaCfg( S, K, vol, r * 100, "call", "european", 80000 ) );
    auto p = Price( VanillaCfg( S, K, vol, r * 100, "put", "european", 80000 ) );
    double parity = S - K * std::exp( -r * T1 );
    // both legs carry MC error
    CHECK( std::abs( ( Premium( c ) - Premium( p ) ) - parity ) <= 6.0 * ( Trust( c ) + Trust( p ) ) + 1e-3 );
}
