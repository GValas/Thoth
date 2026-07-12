#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Phoenix autocallable: the coupon detaches per period — paid at every alive
// observation (and at maturity) with S >= coupon_barrier, each flow discounted
// from its own date; optional memory recovers consecutively missed coupons.
// MCL prices both flavours pathwise; the PDE prices the memoryless flavour
// (three-zone observation overwrite) and rejects the memory one (the missed-
// coupon count is not a function of the spot alone on a 1-D grid).

namespace
{

//! the autocallable test book, Phoenix flavour: 2y note, three ~semiannual
//! observations, 3% rates, 20% vol; coupon_pct is PER PERIOD
std::string PhoenixCfg( const std::string& method, double barrier_pct,
                        double coupon_barrier_pct, double coupon_pct, bool memory,
                        int draws = 1 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( draws, 15, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [3, 3]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 20, calendar: cal}\n"
      << "book: !book {contracts: [ac]}\n"
      << "ac: !autocallable {underlying: eq, premium_currency: eur,"
      << " maturity: 2001-12-31, autocall_dates: [2000-06-30, 2000-12-29, 2001-06-29],"
      << " autocall_barrier: " << barrier_pct << ", protection_barrier: 60,"
      << " coupon: " << coupon_pct << ", coupon_barrier: " << coupon_barrier_pct
      << ( memory ? ", coupon_memory: true" : "" )
      << ", nominal: 100}\n";
    return o.str();
}

double D1( double F, double K, double sig, double T )
{
    return std::log( F / K ) / ( sig * std::sqrt( T ) ) + sig * std::sqrt( T ) / 2;
}

//! pure never-called maturity redemption (protection 60): the closed form the
//! autocallable suite already pins
double RedemptionRef()
{
    const double T = 730.0 / 365.0;
    const double F = 100 * std::exp( 0.03 * T ), df = std::exp( -0.03 * T );
    const double d1 = D1( F, 60, 0.20, T );
    const double d2 = d1 - 0.20 * std::sqrt( T );
    return df * ( 100 * NormCdf( d2 ) + F * NormCdf( -d1 ) );
}

} // namespace

// Certain coupons: an unreachable autocall barrier and a near-zero coupon
// barrier make every observation pay c*N with certainty — the note is the pure
// maturity redemption plus a strip of riskless coupons at the schedule dates.
// Memory changes nothing (no coupon is ever missed): three-way equality.
TEST_CASE( "phoenix: certain coupons price as a riskless strip, memory is a no-op" )
{
    const double c = 2.0; //!< 2 per period on 100 nominal
    double strip = 0;
    for ( double t : { 181.0 / 365.0, 363.0 / 365.0, 545.0 / 365.0, 730.0 / 365.0 } )
    {
        strip += c * std::exp( -0.03 * t );
    }
    const double ref = RedemptionRef() + strip;

    auto mr = Price( PhoenixCfg( "mcl", 100000, 0.0001, c, false, 200000 ) );
    CHECK( std::abs( Premium( mr ) - ref ) <= 6.0 * Trust( mr ) + 1e-2 );

    double pde = Premium( Price( PhoenixCfg( "pde", 100000, 0.0001, c, false ) ) );
    CHECK( pde == doctest::Approx( ref ).epsilon( 5e-3 ) );

    //! memory on identical paths: no coupon is ever missed -> the same price
    auto mm = Price( PhoenixCfg( "mcl", 100000, 0.0001, c, true, 200000 ) );
    CHECK( Premium( mm ) == doctest::Approx( Premium( mr ) ).epsilon( 1e-12 ) );
}

// Unreachable coupon barrier: no coupon ever pays, with or without memory —
// the note collapses to the pure maturity redemption.
TEST_CASE( "phoenix: an unreachable coupon barrier collapses to the redemption" )
{
    //! coupon barrier == autocall barrier, both unreachable
    auto mr = Price( PhoenixCfg( "mcl", 100000, 100000, 2.0, true, 200000 ) );
    CHECK( std::abs( Premium( mr ) - RedemptionRef() ) <= 6.0 * Trust( mr ) + 1e-2 );
}

// The live memoryless Phoenix: PDE three-zone induction vs pathwise MCL.
TEST_CASE( "phoenix: PDE and MCL agree on a live memoryless note" )
{
    double pde = Premium( Price( PhoenixCfg( "pde", 100, 70, 2.0, false ) ) );
    auto mr = Price( PhoenixCfg( "mcl", 100, 70, 2.0, false, 200000 ) );
    CAPTURE( pde );
    CAPTURE( Premium( mr ) );
    CHECK( std::abs( Premium( mr ) - pde ) <= 6.0 * Trust( mr ) + 0.35 );
}

// Memory dominance: on IDENTICAL Sobol paths the memory payoff dominates the
// memoryless one pathwise (missed coupons can only be recovered, never lost),
// strictly so with a coupon barrier the spot crosses often.
TEST_CASE( "phoenix: coupon memory dominates the memoryless note pathwise" )
{
    auto plain = Price( PhoenixCfg( "mcl", 100, 95, 2.0, false, 100000 ) );
    auto memory = Price( PhoenixCfg( "mcl", 100, 95, 2.0, true, 100000 ) );
    CHECK( Premium( memory ) > Premium( plain ) ); //!< same draws: strict dominance
    //! and the recovery is bounded by the coupons that can be missed at all
    CHECK( Premium( memory ) - Premium( plain ) < 4 * 2.0 );
}

// Memory magnitude oracle: with a ~zero vol the path is deterministic
// (S(t) = 100 e^{rt} = 101.5, 103.0, 104.6, 106.2 at the schedule dates), so a
// 102.5% coupon barrier misses EXACTLY the first observation and recovers it at
// the second: the memory premium over the memoryless note is c * df(t2), and
// the absolute memoryless price is the deterministic flow strip.
TEST_CASE( "phoenix: deterministic path pins the memory catch-up magnitude" )
{
    const double c = 2.0, r = 0.03;
    const double t2 = 363.0 / 365.0, t3 = 545.0 / 365.0, T = 730.0 / 365.0;

    auto deterministic = []( bool memory )
    {
        std::string cfg = PhoenixCfg( "mcl", 100000, 102.5, 2.0, memory, 200 );
        const std::string from = "volatility: 20", to = "volatility: 0.0001";
        cfg.replace( cfg.find( from ), from.size(), to );
        return Premium( Price( cfg ) );
    };
    const double plain = deterministic( false );
    const double memory = deterministic( true );

    //! memoryless: coupons at t2, t3, T (t1 missed), nominal redeemed at T
    const double ref_plain = 100 * std::exp( -r * T ) +
                             c * ( std::exp( -r * t2 ) + std::exp( -r * t3 ) +
                                   std::exp( -r * T ) );
    CHECK( plain == doctest::Approx( ref_plain ).epsilon( 1e-6 ) );
    //! the catch-up recovers exactly the missed t1 coupon, paid at t2
    CHECK( memory - plain == doctest::Approx( c * std::exp( -r * t2 ) ).epsilon( 1e-6 ) );
}

// Rejections: PDE + memory, memory without a coupon barrier, coupon barrier
// above the autocall barrier.
TEST_CASE( "phoenix: rejections" )
{
    CHECK_THROWS_AS( Price( PhoenixCfg( "pde", 100, 70, 2.0, true ) ),
                     std::runtime_error );

    std::string no_barrier = PhoenixCfg( "mcl", 100, 70, 2.0, true, 100 );
    const std::string from = " coupon_barrier: 70,", to = "";
    no_barrier.replace( no_barrier.find( from ), from.size(), to );
    CHECK_THROWS_AS( Price( no_barrier ), std::runtime_error );

    CHECK_THROWS_AS( Price( PhoenixCfg( "mcl", 100, 120, 2.0, false, 100 ) ),
                     std::runtime_error );
}
