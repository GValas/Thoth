#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;
using doctest::Approx;

// Continuous dividends enter the engine forward (F = S e^{(r-q)T}). These checks
// use method: ana for a clean closed form; the carry yield (dividend + repo) is
// now consistent across ana / pde / mcl (see test_repo.cpp for the cross-engine
// check).

namespace
{
//! single european vanilla on a dividend-paying equity, priced analytically.
std::string DivCfg( double spot, double strike, double vol_pct, double rate_pct,
                    double div_pct, const std::string& type )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( "ana", 1, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << rate_pct << ", " << rate_pct << "]}\n"
      << "div: !continuous_dividends_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << div_pct << ", " << div_pct << "]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: " << spot
      << ", volatility: vol, currency: eur, continuous_dividends: div}\n"
      << "vol: !bs_volatility {volatility: " << vol_pct << ", calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: " << strike
      << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: european}\n";
    return o.str();
}

//! carry-adjusted Black-Scholes: F = S e^{(r-q)T}, discounted at r.
double BsCarry( double S, double K, double r, double q, double sig, double T,
                const std::string& type )
{
    double F = S * std::exp( ( r - q ) * T );
    double df = std::exp( -r * T );
    double d1 = ( std::log( F / K ) + 0.5 * sig * sig * T ) / ( sig * std::sqrt( T ) );
    double d2 = d1 - sig * std::sqrt( T );
    double c = df * ( F * NormCdf( d1 ) - K * NormCdf( d2 ) );
    return type == "call" ? c : c - df * ( F - K ); //!< put-call parity
}
} // namespace

TEST_CASE( "continuous dividends are carried in the forward (analytic)" )
{
    const double S = 100, K = 100, r = 0.08, q = 0.04, vol = 0.30;

    double call = Premium( Price( DivCfg( S, K, vol * 100, r * 100, q * 100, "call" ) ) );
    double put = Premium( Price( DivCfg( S, K, vol * 100, r * 100, q * 100, "put" ) ) );

    CHECK( call == Approx( BsCarry( S, K, r, q, vol, T1, "call" ) ).epsilon( 1e-6 ) );
    CHECK( put == Approx( BsCarry( S, K, r, q, vol, T1, "put" ) ).epsilon( 1e-6 ) );

    // a positive dividend makes the call cheaper and the put dearer than no-div
    CHECK( call < BsCall( S, K, r, vol, T1 ) );
    CHECK( put > BsPut( S, K, r, vol, T1 ) );
}

TEST_CASE( "zero dividend reproduces plain Black-Scholes" )
{
    const double S = 100, K = 110, r = 0.06, vol = 0.25;
    double call = Premium( Price( DivCfg( S, K, vol * 100, r * 100, 0, "call" ) ) );
    CHECK( call == Approx( BsCall( S, K, r, vol, T1 ) ).epsilon( 1e-6 ) );
}

namespace
{
//! a 5-year American put on an equity paying five annual cash dividends (escrowed
//! model), priced by the requested method. The dividend schedule and discount
//! curve match across engines, so PDE and MCL must agree.
std::string AmDivCfg( const std::string& method, int draws )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( method, draws, 7, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "divs: !discrete_dividends {dates: [2000-12-31, 2001-12-31, 2002-12-31,"
      << " 2003-12-31, 2004-12-31], amounts: [3, 3, 3, 3, 3]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur, discrete_dividends: divs}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2004-12-31, nominal: 1, type: put,"
      << " exercise: american}\n";
    return o.str();
}
} // namespace

// Escrowed-dividend American exercise: the holder exercises against the OBSERVED
// spot (escrowed value + future-dividend PV), not the dividend-stripped escrowed
// value. Testing on the escrowed value alone over-values the option (here it would
// read ~18.6 instead of ~14.15). The PDE adds back the future-dividend PV at each
// time step so it matches the MCL engine and a binomial escrowed-plus reference.
TEST_CASE( "american put on discrete dividends agrees across PDE and MCL" )
{
    //! escrowed-plus binomial reference (Schroder) for this schedule ~ 14.15
    const double ref = 14.15;

    const double pde = Premium( Price( AmDivCfg( "pde", 1 ) ) );
    auto mcl_res = Price( AmDivCfg( "mcl", 200000 ) );
    const double mcl = Premium( mcl_res );

    CAPTURE( pde );
    CAPTURE( mcl );
    CHECK( pde == Approx( ref ).epsilon( 0.01 ) );                  //!< PDE on the tree reference
    CHECK( std::abs( pde - mcl ) <= 6.0 * Trust( mcl_res ) + 0.1 ); //!< PDE vs MC within error
    CHECK( pde < 17.0 );                                            //!< guards the old escrowed-intrinsic bug (~18.6)
}
