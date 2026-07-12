#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Autocallable (Athena): first autocall date with S >= barrier redeems at
// nominal*(1 + k*coupon); at maturity the redemption profile pays the accrued
// coupon above the autocall level, the nominal above the protection level, and
// nominal*S/S_ref below. MCL prices it pathwise (one flow node per schedule
// date); the PDE runs a backward induction overwriting the layer with the
// accrued rebate above the level at each observation step; ANA rejects.

namespace
{

//! 2y note, three ~semiannual autocall dates, 3% rates, 20% flat vol
std::string AutocallCfg( const std::string& method, double barrier_pct,
                         double protection_pct, double coupon_pct, int draws = 1,
                         const std::string& rate_model = "" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( draws, 15, 5 )
      << "eur: !currency {rate: rate" << rate_model << "}\n"
      << "hw: !hull_white {mean_reversion: 0.1, volatility: 1.5}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [3, 3]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eur_ir],"
      << " matrix: [1, -0.3, -0.3, 1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 20, calendar: cal}\n"
      << "book: !book {contracts: [ac]}\n"
      << "ac: !autocallable {underlying: eq, premium_currency: eur,"
      << " maturity: 2001-12-31, autocall_dates: [2000-06-30, 2000-12-29, 2001-06-29],"
      << " autocall_barrier: " << barrier_pct << ", protection_barrier: " << protection_pct
      << ", coupon: " << coupon_pct << ", nominal: 100}\n";
    return o.str();
}

//! Black-Scholes building blocks on the maturity redemption (never autocalled)
double D1( double F, double K, double sig, double T )
{
    return std::log( F / K ) / ( sig * std::sqrt( T ) ) + sig * std::sqrt( T ) / 2;
}

} // namespace

// Degenerate floor: a near-zero autocall barrier triggers at the FIRST
// observation on every path, so the note is a zero-coupon paying 108 at t1 —
// closed form df(t1)*108, with zero MC variance.
TEST_CASE( "autocallable: an always-triggered note is a zero coupon to the first date" )
{
    const double t1 = 181.0 / 365.0; //!< 2000-01-01 -> 2000-06-30
    const double ref = std::exp( -0.03 * t1 ) * 108.0;

    auto mr = Price( AutocallCfg( "mcl", 0.0001, 0.0, 8, 20000 ) );
    //! every path pays the same flow, so the MC mean is exact (the zero-variance
    //! trust itself degenerates to nan in the standard-error formula — not asserted)
    CHECK( Premium( mr ) == doctest::Approx( ref ).epsilon( 1e-9 ) );

    double pde = Premium( Price( AutocallCfg( "pde", 0.0001, 0.0, 8 ) ) );
    //! the observation date maps to the nearest grid step -> small time offset
    CHECK( pde == doctest::Approx( ref ).epsilon( 2e-3 ) );
}

// Degenerate ceiling: an unreachable autocall barrier never triggers, so the
// note is the pure maturity redemption — nominal above the protection level,
// linear below: price = df*(N*Phi(d2(Bp)) + (N/S0)*F*Phi(-d1(Bp))). Both
// engines must match that closed form.
TEST_CASE( "autocallable: a never-triggered note prices the maturity redemption" )
{
    const double T = 730.0 / 365.0; //!< 2000-01-01 -> 2001-12-31
    const double F = 100 * std::exp( 0.03 * T ), df = std::exp( -0.03 * T );
    const double Bp = 60, sig = 0.20;
    const double d1 = D1( F, Bp, sig, T );
    const double d2 = d1 - sig * std::sqrt( T );
    const double ref = df * ( 100 * NormCdf( d2 ) + F * NormCdf( -d1 ) );

    auto mr = Price( AutocallCfg( "mcl", 100000, 60, 8, 200000 ) );
    CHECK( std::abs( Premium( mr ) - ref ) <= 6.0 * Trust( mr ) + 1e-2 );

    double pde = Premium( Price( AutocallCfg( "pde", 100000, 60, 8 ) ) );
    CHECK( pde == doctest::Approx( ref ).epsilon( 5e-3 ) );
}

// The genuine product: ATM autocall barrier, 60% protection, 8% snowball —
// the PDE backward induction and the pathwise MCL must agree.
TEST_CASE( "autocallable: PDE and MCL agree on a live note" )
{
    double pde = Premium( Price( AutocallCfg( "pde", 100, 60, 8 ) ) );
    auto mr = Price( AutocallCfg( "mcl", 100, 60, 8, 200000 ) );
    double mcl = Premium( mr );

    CAPTURE( pde );
    CAPTURE( mcl );
    //! grid step-mapping of the observation dates + digital discontinuities
    CHECK( std::abs( mcl - pde ) <= 6.0 * Trust( mr ) + 0.35 );

    //! sanity: the note is worth less than the always-called zero coupon and
    //! more than the never-called redemption floor of the same booking
    CHECK( mcl < std::exp( -0.03 * 0.5 ) * 108.0 );
    CHECK( mcl > 60.0 );
}

// Composition with the Hull-White hybrid: the trigger flows discount pathwise,
// so the note prices under stochastic rates out of the box; sigma_r -> 0 must
// reproduce the deterministic-rate price within the MC error.
TEST_CASE( "autocallable: prices under Hull-White, sigma_r -> 0 reduces to deterministic" )
{
    auto det = Price( AutocallCfg( "mcl", 100, 60, 8, 150000 ) );

    std::string hw0 = AutocallCfg( "mcl", 100, 60, 8, 150000, ", rate_model: hw" );
    const std::string from = "volatility: 1.5", to = "volatility: 0.0";
    hw0.replace( hw0.find( from ), from.size(), to );
    auto r0 = Price( hw0 );
    CHECK( std::abs( Premium( r0 ) - Premium( det ) ) <=
           6.0 * ( Trust( r0 ) + Trust( det ) ) + 1e-2 );

    //! live rate vol: the price moves (negative equity/rate correlation) but
    //! stays a sane note value
    auto rhw = Price( AutocallCfg( "mcl", 100, 60, 8, 150000, ", rate_model: hw" ) );
    CHECK( Premium( rhw ) > 60.0 );
    CHECK( Premium( rhw ) < 108.0 );
}

// Theta rolls today one day forward: a note whose FIRST observation is
// tomorrow then has an observation exactly ON the (rolled) today — which is
// observed at the live spot, not rejected as seasoned. The PDE folds it into
// its first grid step, so the theta Greek prices instead of aborting.
TEST_CASE( "autocallable: theta prices a note whose first observation is tomorrow" )
{
    std::string cfg = AutocallCfg( "pde", 100, 60, 8 );
    std::string from = "autocall_dates: [2000-06-30,";
    std::string to = "autocall_dates: [2000-01-02,";
    cfg.replace( cfg.find( from ), from.size(), to );
    from = "indicators: [premium]";
    to = "indicators: [premium, theta]";
    cfg.replace( cfg.find( from ), from.size(), to );

    auto r = Price( cfg ); //!< must not throw the seasoned-schedule error
    CHECK( std::isfinite( Greek( r, "theta" ) ) );
    CHECK( Premium( r ) > 60.0 );
    CHECK( Premium( r ) < 140.0 );
}

// Rejections: ANA has no closed form; malformed schedules fail at load.
TEST_CASE( "autocallable: rejections" )
{
    CHECK_THROWS_AS( Price( AutocallCfg( "ana", 100, 60, 8 ) ), std::runtime_error );

    //! protection above the autocall barrier
    CHECK_THROWS_AS( Price( AutocallCfg( "mcl", 80, 90, 8, 100 ) ), std::runtime_error );

    //! an autocall date at/after maturity
    std::string bad = AutocallCfg( "mcl", 100, 60, 8, 100 );
    const std::string from = "2001-06-29", to = "2002-06-29";
    bad.replace( bad.find( from ), from.size(), to );
    CHECK_THROWS_AS( Price( bad ), std::runtime_error );

    //! an autocall date before today
    std::string past = AutocallCfg( "mcl", 100, 60, 8, 100 );
    const std::string from2 = "2000-06-30", to2 = "1999-06-30";
    past.replace( past.find( from2 ), from2.size(), to2 );
    CHECK_THROWS_AS( Price( past ), std::runtime_error );
}
