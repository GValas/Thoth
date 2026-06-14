#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// American call on a non-dividend underlying is never worth exercising early,
// so its value collapses to the European (Black-Scholes) call.
TEST_CASE( "American call (no dividend) equals the European call" )
{
    const double S = 100, r = 0.08, vol = 30, K = 100;

    // PDE American oracle
    auto pde = Price( VanillaCfg( S, K, vol, r * 100, "call", "american", 1, /*pde=*/true, 5 ) );
    double bs = BsCall( S, K, r, vol / 100, T1 );
    CHECK( std::abs( Premium( pde ) - bs ) <= 0.05 );

    // Longstaff-Schwartz Monte-Carlo must also recover it (within MC error)
    auto mcl = Price( VanillaCfg( S, K, vol, r * 100, "call", "american", 200000 ) );
    CHECK( std::abs( Premium( mcl ) - bs ) <= 6.0 * Trust( mcl ) + 5e-2 );
}

// American put carries a positive early-exercise premium over the European put.
TEST_CASE( "American put exceeds the European put and matches the PDE oracle" )
{
    const double S = 100, r = 0.08, vol = 30, K = 100;

    double euro_bs = BsPut( S, K, r, vol / 100, T1 );

    // PDE American oracle (deterministic backward induction)
    auto pde = Price( VanillaCfg( S, K, vol, r * 100, "put", "american", 1, /*pde=*/true, 5 ) );
    double pde_amer = Premium( pde );
    CAPTURE( euro_bs );
    CAPTURE( pde_amer );
    CHECK( pde_amer > euro_bs ); //!< early-exercise premium is positive

    // Longstaff-Schwartz lower bound: close to, and not above, the PDE oracle by much.
    auto mcl = Price( VanillaCfg( S, K, vol, r * 100, "put", "american", 300000 ) );
    double lsm = Premium( mcl );
    CAPTURE( lsm );
    CHECK( lsm > euro_bs ); //!< still above the European value
    // LSM is a (biased-low) approximation of the true American value
    CHECK( lsm <= pde_amer + 6.0 * Trust( mcl ) + 5e-2 );
    CHECK( lsm >= pde_amer * 0.95 ); //!< within ~5% of the oracle
}

// Deep in-the-money American put should be very close to its intrinsic value
// (immediate exercise dominates), a strong early-exercise signal.
TEST_CASE( "deep ITM American put is near intrinsic" )
{
    const double S = 60, K = 100, r = 0.08, vol = 30;
    auto pde = Price( VanillaCfg( S, K, vol, r * 100, "put", "american", 1, true, 5 ) );
    double intrinsic = K - S;                    //!< = 40
    CHECK( Premium( pde ) >= intrinsic - 1e-2 ); //!< at least intrinsic
    CHECK( Premium( pde ) <= intrinsic + 2.0 );  //!< small time value left
}
