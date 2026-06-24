#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// The Crank-Nicolson PDE is deterministic, so it can be held to a tight,
// fixed tolerance against Black-Scholes (no Monte-Carlo standard error).
TEST_CASE( "European vanilla PDE matches Black-Scholes" )
{
    const double S = 100, r = 0.08;
    const int unused_draws = 1000; //!< MC drawings ignored when method=pde

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
                                              unused_draws, /*pde=*/true, /*precision=*/5 ) );
                double pde = Premium( res );
                double bs = ( type == "call" )
                                ? BsCall( S, K, r, vol / 100, T1 )
                                : BsPut( S, K, r, vol / 100, T1 );
                // discretisation error only — no statistical noise
                CHECK( std::abs( pde - bs ) <= 0.05 );
            }
        }
    }
}

TEST_CASE( "PDE put-call parity (deterministic)" )
{
    const double S = 100, r = 0.08, vol = 30, K = 110;
    auto c = Price( VanillaCfg( S, K, vol, r * 100, "call", "european", 1, true, 5 ) );
    auto p = Price( VanillaCfg( S, K, vol, r * 100, "put", "european", 1, true, 5 ) );
    double parity = S - K * std::exp( -r * T1 );
    CHECK( ( Premium( c ) - Premium( p ) ) == doctest::Approx( parity ).epsilon( 1e-3 ) );
}

TEST_CASE( "PDE is repeatable to the bit (no randomness)" )
{
    auto a = Price( VanillaCfg( 100, 100, 30, 8, "put", "european", 1, true, 5 ) );
    auto b = Price( VanillaCfg( 100, 100, 30, 8, "put", "european", 1, true, 5 ) );
    CHECK( Premium( a ) == Premium( b ) );
}
