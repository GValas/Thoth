#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;
using doctest::Approx;

// Continuously-monitored single-barrier options priced by the closed-form
// (Reiner-Rubinstein) engine, driven through the !ana_pricer.
// Reference values were produced by an independent Python implementation that
// was itself cross-checked against a Brownian-bridge Monte-Carlo simulation.
// All cases: T = 1y, no dividends (cost-of-carry b = r).

TEST_CASE( "barrier closed-form matches Reiner-Rubinstein reference values" )
{
    struct Case
    {
        double S, K, H, vol, rate;
        std::string type, barrier;
        double ref;
    };

    // S, K, H, vol%, rate%, type, barrier_type, expected premium
    for ( Case c : {
              Case{ 100, 100, 90, 25, 5, "call", "down&out", 9.111221 },
              Case{ 100, 100, 90, 25, 5, "call", "down&in", 3.224778 },
              Case{ 100, 100, 120, 25, 5, "call", "up&out", 0.691324 },
              Case{ 100, 100, 120, 25, 5, "call", "up&in", 11.644675 },
              Case{ 100, 100, 120, 25, 5, "put", "up&in", 0.656074 },
              Case{ 100, 95, 90, 30, 8, "put", "down&out", 0.006398 } } )
    {
        CAPTURE( c.type );
        CAPTURE( c.barrier );
        CAPTURE( c.H );
        auto res = Price( BarrierCfg( c.S, c.K, c.H, c.vol, c.rate, c.type, c.barrier ) );
        CHECK( std::abs( Premium( res ) - c.ref ) <= 1e-3 );
    }
}

// The Crank-Nicolson PDE prices the same barriers (knock-out by a zero Dirichlet
// condition on the barrier, knock-in by in/out parity) and must agree with the
// closed form to grid-discretisation accuracy.
TEST_CASE( "barrier PDE matches the closed-form reference" )
{
    struct Case
    {
        double S, K, H, vol, rate;
        std::string type, barrier;
        double ref;
    };

    for ( Case c : {
              Case{ 100, 100, 90, 25, 5, "call", "down&out", 9.111221 },
              Case{ 100, 100, 90, 25, 5, "call", "down&in", 3.224778 },
              Case{ 100, 100, 120, 25, 5, "call", "up&out", 0.691324 },
              Case{ 100, 100, 120, 25, 5, "call", "up&in", 11.644675 },
              Case{ 100, 100, 120, 25, 5, "put", "up&in", 0.656074 } } )
    {
        CAPTURE( c.type );
        CAPTURE( c.barrier );
        CAPTURE( c.H );
        auto res = Price( BarrierCfg( c.S, c.K, c.H, c.vol, c.rate, c.type, c.barrier,
                                      /*method=*/"pde", /*precision=*/5 ) );
        CHECK( std::abs( Premium( res ) - c.ref ) <= 0.05 );
    }
}

// The Monte-Carlo (MCL) engine monitors the barrier along each simulated path
// (with a Broadie-Glasserman-Kou continuity correction) and must agree with the
// closed form within Monte-Carlo error.
TEST_CASE( "barrier MCL matches the closed-form reference" )
{
    struct Case
    {
        double S, K, H, vol, rate;
        std::string type, barrier;
        double ref;
    };

    for ( Case c : {
              Case{ 100, 100, 90, 25, 5, "call", "down&out", 9.111221 },
              Case{ 100, 100, 120, 25, 5, "call", "up&out", 0.691324 },
              Case{ 100, 100, 120, 25, 5, "call", "up&in", 11.644675 },
              Case{ 100, 100, 90, 25, 5, "call", "down&in", 3.224778 } } )
    {
        CAPTURE( c.type );
        CAPTURE( c.barrier );
        CAPTURE( c.H );
        auto res = Price( BarrierCfg( c.S, c.K, c.H, c.vol, c.rate, c.type, c.barrier,
                                      /*method=*/"mcl", /*precision=*/5,
                                      /*draws=*/80000, /*mcl_step=*/3 ) );
        CHECK( std::abs( Premium( res ) - c.ref ) <= 0.20 );
    }
}

// In/out parity: a knock-in plus the matching knock-out reproduce the vanilla.
TEST_CASE( "barrier in/out parity reproduces the vanilla" )
{
    const double S = 100, K = 105, vol = 30, rate = 6, r = 0.06;

    struct Pair
    {
        std::string out, in;
        double H;
        std::string type;
    };
    for ( Pair p : { Pair{ "down&out", "down&in", 85, "call" },
                     Pair{ "up&out", "up&in", 130, "call" },
                     Pair{ "down&out", "down&in", 85, "put" },
                     Pair{ "up&out", "up&in", 130, "put" } } )
    {
        CAPTURE( p.type );
        CAPTURE( p.H );
        double out = Premium( Price( BarrierCfg( S, K, p.H, vol, rate, p.type, p.out ) ) );
        double in = Premium( Price( BarrierCfg( S, K, p.H, vol, rate, p.type, p.in ) ) );
        double bs = ( p.type == "call" )
                        ? BsCall( S, K, r, vol / 100, T1 )
                        : BsPut( S, K, r, vol / 100, T1 );
        CHECK( ( out + in ) == Approx( bs ).epsilon( 1e-9 ) );
    }
}

// A barrier placed far out of the money is (almost) never touched, so the
// knock-out collapses to the vanilla and the knock-in to nearly zero.
TEST_CASE( "far barrier collapses to the vanilla / zero" )
{
    const double S = 100, K = 100, vol = 25, rate = 5, r = 0.05;
    double bs = BsCall( S, K, r, vol / 100, T1 );

    double dout = Premium( Price( BarrierCfg( S, K, 1, vol, rate, "call", "down&out" ) ) );
    double uout = Premium( Price( BarrierCfg( S, K, 1e6, vol, rate, "call", "up&out" ) ) );
    double din = Premium( Price( BarrierCfg( S, K, 1, vol, rate, "call", "down&in" ) ) );

    CHECK( dout == Approx( bs ).epsilon( 1e-6 ) );
    CHECK( uout == Approx( bs ).epsilon( 1e-6 ) );
    CHECK( std::abs( din ) <= 1e-6 );
}
