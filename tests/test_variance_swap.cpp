#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Variance swap. Under a flat vol the fair (annualised) variance is sigma^2, so
//   PV = notional * DF * (sigma^2 - K_var),  K_var = (strike_vol)^2.
// The ANA pricer integrates the OTM option strip (Demeterfi-Derman-Kamal-Zou) and
// must reproduce that; the PDE pricer solves the expected-accumulated-variance
// backward grid and must agree with the ANA value.

namespace
{
//! one variance swap on a single flat-vol equity. vol_pct / rate_pct in percent;
//! strike_vol_pct is the variance strike expressed as a vol, in percent.
std::string VarSwapCfg( double vol_pct, double rate_pct, double strike_vol_pct,
                        double notional, const std::string& method )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( 1, 30, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << rate_pct << ", " << rate_pct << "]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: " << vol_pct << ", calendar: cal}\n"
      << "book: !book {contracts: [vs]}\n"
      << "vs: !variance_swap {underlying: eq, premium_currency: eur,"
      << " maturity: 2000-12-31, volatility_strike: " << strike_vol_pct
      << ", notional: " << notional << "}\n";
    return o.str();
}

//! analytic fair-variance PV under a flat vol
double VarSwapPV( double vol, double rate, double strike_vol, double notional )
{
    const double df = std::exp( -rate * T1 );
    return notional * df * ( vol * vol - strike_vol * strike_vol );
}
} // namespace

TEST_CASE( "variance swap ANA reproduces the flat-vol fair-variance PV" )
{
    const double vol = 0.30, r = 0.05, kvol = 0.20, notl = 10000;
    const double pv = VarSwapPV( vol, r, kvol, notl );

    auto res = Price( VarSwapCfg( vol * 100, r * 100, kvol * 100, notl, "ana" ) );
    CAPTURE( Premium( res ) );
    CAPTURE( pv );
    //! strip integration is a numerical approximation of sigma^2 -> ~1% band
    CHECK( Premium( res ) == doctest::Approx( pv ).epsilon( 0.01 ) );
    CHECK( Premium( res ) > 0 ); //!< vol (30) above strike (20) -> long variance pays
}

TEST_CASE( "variance swap PDE agrees with the ANA fair variance" )
{
    const double vol = 0.30, r = 0.05, kvol = 0.20, notl = 10000;
    const double ana = Premium( Price( VarSwapCfg( vol * 100, r * 100, kvol * 100, notl, "ana" ) ) );
    const double pde = Premium( Price( VarSwapCfg( vol * 100, r * 100, kvol * 100, notl, "pde" ) ) );
    CAPTURE( ana );
    CAPTURE( pde );
    //! PDE accumulated-variance grid vs the analytic strip -> small grid tolerance
    CHECK( pde == doctest::Approx( ana ).epsilon( 0.02 ) );
}

TEST_CASE( "variance swap is ~zero when the strike equals the vol" )
{
    const double vol = 0.25, r = 0.05, notl = 10000;
    auto res = Price( VarSwapCfg( vol * 100, r * 100, vol * 100, notl, "ana" ) );
    //! sigma^2 - K_var = 0, so only the strip's tiny integration residual remains
    CHECK( std::abs( Premium( res ) ) <= 0.01 * notl );
}
