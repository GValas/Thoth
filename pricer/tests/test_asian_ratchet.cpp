#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Asian (arithmetic average-price) and Ratchet (cliquet) — both path-dependent,
// Monte-Carlo only. The clean degenerate cases pin them in closed form: a
// single-observation Asian is exactly a vanilla; a locally-flat ratchet is a
// deterministic coupon.

namespace
{

std::string Market()
{
    //! CfgBlock defines cfg_mcl / cfg_pde (the objects ConfigRef points at); 200k
    //! Sobol paths, daily steps, high PDE precision (though PDE rejects these).
    return CfgBlock( 200000, 5, 5 ) +
           "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
           "eur: !currency {rate: rate}\n"
           "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
           "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
           "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
           "vol: !bs_volatility {volatility: 30, calendar: cal}\n";
}

std::string AsianCfg( const std::string& method, int obs_days, const std::string& type = "call" )
{
    std::ostringstream o;
    o << "root: p\n"
      << "p: !" << method << "_pricer {today: 2000-01-01, book: bk, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << Market()
      << "bk: !book {contracts: [c]}\n"
      << "c: !asian {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, type: " << type
      << ", nominal: 1, observation_period_days: " << obs_days << "}\n";
    return o.str();
}

std::string RatchetCfg( const std::string& method, double lf, double lc, double gf,
                        const std::string& global_cap = "" )
{
    std::ostringstream o;
    o << "root: p\n"
      << "p: !" << method << "_pricer {today: 2000-01-01, book: bk, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << Market()
      << "bk: !book {contracts: [c]}\n"
      << "c: !ratchet {underlying: eq, premium_currency: eur, maturity: 2000-12-31,"
      << " nominal: 100, observation_period_days: 90, local_floor: " << lf
      << ", local_cap: " << lc << ", global_floor: " << gf
      << ( global_cap.empty() ? "" : ", global_cap: " + global_cap ) << "}\n";
    return o.str();
}

} // namespace

// A single-observation Asian (the averaging period exceeds the maturity, so the
// only fixing is maturity) averages just S_T -> it is exactly a European vanilla.
TEST_CASE( "asian: a single-observation average is a vanilla" )
{
    const double bs = BsCall( 100, 100, 0.05, 0.30, T1 );
    auto mr = Price( AsianCfg( "mcl", 400 ) ); //!< 400d > 1y -> only the maturity fixing
    CHECK( std::abs( Premium( mr ) - bs ) <= 6.0 * Trust( mr ) + 1e-2 );
}

// Averaging damps the terminal-spot variance, so a monthly-averaged Asian is
// strictly cheaper than the same-strike vanilla (its single-fixing limit).
TEST_CASE( "asian: monthly averaging is cheaper than the vanilla" )
{
    const double vanilla = Premium( Price( AsianCfg( "mcl", 400 ) ) );
    auto mr = Price( AsianCfg( "mcl", 30 ) );
    CHECK( Premium( mr ) < vanilla - 0.5 ); //!< well below, beyond the MC noise
    CHECK( Premium( mr ) > 0 );
}

// A locally-flat ratchet (local_floor == local_cap == 0) clips every period
// return to 0, so the coupon is exactly the global floor: a deterministic
// premium N * global_floor * df, with zero MC variance.
TEST_CASE( "ratchet: a locally-flat note pays the deterministic global floor" )
{
    const double df = std::exp( -0.05 * T1 );

    //! global floor 0 -> the coupon is 0 -> the note is worth nothing
    CHECK( Premium( Price( RatchetCfg( "mcl", 0, 0, 0 ) ) ) == doctest::Approx( 0.0 ).epsilon( 1e-9 ) );

    //! global floor 5% -> coupon clamps up to 5% -> premium = 100 * 0.05 * df
    auto mr = Price( RatchetCfg( "mcl", 0, 0, 5 ) );
    CHECK( Premium( mr ) == doctest::Approx( 100 * 0.05 * df ).epsilon( 1e-6 ) );
}

// A live ratchet (+/-5% local clip, floor 0, four quarterly periods) is a real
// positive coupon, bounded above by the capped-sum ceiling (4 * 5% here).
TEST_CASE( "ratchet: a live note is a bounded positive coupon" )
{
    const double df = std::exp( -0.05 * T1 );
    auto mr = Price( RatchetCfg( "mcl", -5, 5, 0 ) );
    CHECK( Premium( mr ) > 0 );
    //! at most 4 periods * 5% cap, discounted (a loose but valid upper bound)
    CHECK( Premium( mr ) < 100 * 4 * 0.05 * df + 6.0 * Trust( mr ) );
}

// Both products are Monte-Carlo only: the analytic and PDE engines reject them
// (no closed form / no 1-D grid state for the path average or the coupon sum).
TEST_CASE( "asian / ratchet: ANA and PDE reject the path-dependent payoff" )
{
    CHECK_THROWS_AS( Price( AsianCfg( "ana", 30 ) ), std::runtime_error );
    CHECK_THROWS_AS( Price( AsianCfg( "pde", 30 ) ), std::runtime_error );
    CHECK_THROWS_AS( Price( RatchetCfg( "ana", -5, 5, 0 ) ), std::runtime_error );
    CHECK_THROWS_AS( Price( RatchetCfg( "pde", -5, 5, 0 ) ), std::runtime_error );
}
