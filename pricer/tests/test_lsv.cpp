#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// LSV (local-stochastic volatility): a Heston variance factor whose spot
// diffusion carries a leverage L(S,t) calibrated so the model reprices a target
// implied surface. The defining property — and the main thing these tests pin —
// is that LSV vanillas match the TARGET surface's price (here priced by ANA on
// a twin equity carrying the target vol directly), whatever the Heston base.

namespace
{
//! a vanilla on an LSV equity (target = a skewed SABR surface), priced by the
//! given engine; the same YAML also carries a twin equity on the raw SABR vol so
//! the "ana"+target reference reuses one config shape.
std::string LsvSabrCfg( const std::string& method, double strike, const std::string& type,
                        bool on_target, double vol_of_vol = 0.5 )
{
    std::ostringstream o;
    o << "root: p\n"
      << "p: !" << method << "_pricer {today: 2000-01-01, book: bk, currency: eur," << ConfigRef( method )
      << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( 200000, 5, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [3, 3]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, -0.6, -0.6, 1]}\n"
      << "eq: !equity {spot: 100, volatility: " << ( on_target ? "sabr" : "lsv" ) << ", currency: eur}\n"
      << "sabr: !sabr_volatility {maturities: [0.5, 2], alpha: [0.22, 0.24], beta: [1, 1],"
      << " rho: [-0.5, -0.5], nu: [0.6, 0.5], calendar: cal}\n"
      << "lsv: !lsv_volatility {spot: 100, init_vol: 18, long_vol: 26, kappa: 2,"
      << " vol_of_vol: " << vol_of_vol << ", surface: sabr, calendar: cal}\n"
      << "bk: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: " << strike
      << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: european}\n";
    return o.str();
}
} // namespace

// --- LSV (MCL) : the calibrated model reprices the target SABR smile across
// moneyness — the Heston base (v0 != theta, big vol-of-vol) is fully absorbed
// by the leverage. Reference: ANA on the raw SABR surface.
TEST_CASE( "LSV MCL reprices the target SABR surface across strikes" )
{
    for ( double K : { 80.0, 100.0, 120.0 } )
    {
        CAPTURE( K );
        std::string type = ( K < 100 ) ? "put" : "call";
        double target = Premium( Price( LsvSabrCfg( "ana", K, type, true ) ) );
        auto mr = Price( LsvSabrCfg( "mcl", K, type, false ) );
        CAPTURE( target );
        CAPTURE( Premium( mr ) );
        //! MC error + the binned-particle calibration bias (grid/bin resolution)
        CHECK( std::abs( Premium( mr ) - target ) <= 6.0 * Trust( mr ) + 0.10 );
    }
}

// --- LSV (PDE) : the 2-D ADI grid with the leverage in its S-coefficients
// reprices the same target surface (coarser tolerance: 2-D grid + time-uniform
// leverage lookup).
TEST_CASE( "LSV PDE reprices the target SABR surface" )
{
    for ( double K : { 90.0, 100.0, 110.0 } )
    {
        CAPTURE( K );
        std::string type = ( K < 100 ) ? "put" : "call";
        double target = Premium( Price( LsvSabrCfg( "ana", K, type, true ) ) );
        double pde = Premium( Price( LsvSabrCfg( "pde", K, type, false ) ) );
        CAPTURE( target );
        CAPTURE( pde );
        CHECK( std::abs( pde - target ) <= 0.25 );
    }
}

// --- LSV (MCL) : degenerate limit — flat BS target and vol-of-vol -> 0 makes the
// leverage a deterministic vol ratio, so the model collapses to plain BS.
TEST_CASE( "LSV MCL degenerate limit matches Black-Scholes" )
{
    std::ostringstream o;
    o << "root: p\n"
      << "p: !mcl_pricer {today: 2000-01-01, book: bk, currency: eur, mcl_configuration: cfg_mcl,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( 200000, 7, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, 0, 0, 1]}\n"
      << "eq: !equity {spot: 100, volatility: lsv, currency: eur}\n"
      << "flat: !bs_volatility {volatility: 25, calendar: cal}\n"
      //! Heston base deliberately OFF the target level (30% vs 25%): the leverage
      //! must rescale it to the target
      << "lsv: !lsv_volatility {spot: 100, init_vol: 30, long_vol: 30, kappa: 3,"
      << " vol_of_vol: 0.0001, surface: flat, calendar: cal}\n"
      << "bk: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    auto mr = Price( o.str() );
    CHECK( std::abs( Premium( mr ) - BsCall( 100, 100, 0.05, 0.25, T1 ) ) <= 6.0 * Trust( mr ) + 0.06 );
}

// --- LSV (ANA) : no closed form — the analytic engine must reject the model
// rather than silently pricing the bare Heston characteristic function.
TEST_CASE( "LSV is rejected by the ANA engine" )
{
    CHECK_THROWS_AS( Price( LsvSabrCfg( "ana", 100, "call", false ) ), std::runtime_error );
}

// --- LSV (config) : Bates jumps under LSV are a configuration error (the Dupire
// matching does not strip a jump contribution), as is a stochastic target surface.
TEST_CASE( "LSV configuration rejects jumps and a stochastic target" )
{
    auto cfg = []( const std::string& lsv_fields, const std::string& target )
    {
        std::ostringstream o;
        o << "root: p\n"
          << "p: !mcl_pricer {today: 2000-01-01, book: bk, currency: eur, mcl_configuration: cfg_mcl,"
          << " correlation: cor, indicators: [premium], result: res}\n"
          << CfgBlock( 1000, 7, 5 )
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, 0, 0, 1]}\n"
          << "eq: !equity {spot: 100, volatility: lsv, currency: eur}\n"
          << "flat: !bs_volatility {volatility: 25}\n"
          << "h: !heston_volatility {spot: 100, init_vol: 25, long_vol: 25, kappa: 2, vol_of_vol: 0.5}\n"
          << "lsv: !lsv_volatility {spot: 100, init_vol: 25, long_vol: 25, kappa: 2,"
          << " vol_of_vol: 0.5, surface: " << target << lsv_fields << "}\n"
          << "bk: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
          << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
        return o.str();
    };
    CHECK_THROWS_AS( Price( cfg( ", jump_intensity: 0.5, jump_mean: -0.1, jump_vol: 0.2", "flat" ) ),
                     std::runtime_error );
    CHECK_THROWS_AS( Price( cfg( "", "h" ) ), std::runtime_error );
}
