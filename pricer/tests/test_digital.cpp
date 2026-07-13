#include "helpers.hpp"
#include <doctest/doctest.h>
#include <cmath>

using namespace test;

namespace
{
//! one digital option priced by `method`. payout is "cash_or_nothing" | "asset_or_nothing".
std::string DigitalCfg( const std::string& method, double strike, const std::string& type,
                        const std::string& payout, double cash, int draws )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( draws, 7, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "o: !digital {underlying: eq, premium_currency: eur, strike: " << strike
      << ", is_absolute_strike: true, maturity: 2000-12-31, type: " << type
      << ", payout: " << payout << ", cash_amount: " << cash << "}\n"
      << "book: !book {contracts: [o]}\n";
    return o.str();
}
} // namespace

// The European digital's analytic price (PricerANA::PriceDigital / BS_Digital_Price) is pinned
// by exact model-free identities and cross-checked against the Monte-Carlo flow node, so a
// change to either the payoff or the closed form shows up.
TEST_CASE( "digital: closed-form identities + ANA/MCL agreement" )
{
    const double S = 100, r = 0.05, vol = 0.30;
    const double df = std::exp( -r * T1 );

    for ( double K : { 80.0, 100.0, 120.0 } )
    {
        CAPTURE( K );

        const double cash_c = Premium( Price( DigitalCfg( "ana", K, "call", "cash_or_nothing", 1, 1 ) ) );
        const double cash_p = Premium( Price( DigitalCfg( "ana", K, "put", "cash_or_nothing", 1, 1 ) ) );
        const double asset_c = Premium( Price( DigitalCfg( "ana", K, "call", "asset_or_nothing", 1, 1 ) ) );

        //! cash-or-nothing call + put pays 1 either way (exactly one of S>K / S<K) -> df
        CHECK( ( cash_c + cash_p ) == doctest::Approx( df ).epsilon( 1e-9 ) );

        //! vanilla call = asset-or-nothing call - K * cash-or-nothing call (BS decomposition)
        CHECK( std::abs( ( asset_c - K * cash_c ) - BsCall( S, K, r, vol, T1 ) ) <= 1e-2 );

        //! the MCL flow node reproduces the analytic price (cash and asset legs)
        auto cash_mr = Price( DigitalCfg( "mcl", K, "call", "cash_or_nothing", 1, 300000 ) );
        CHECK( std::abs( Premium( cash_mr ) - cash_c ) <= 6.0 * Trust( cash_mr ) + 2e-3 );
        auto asset_mr = Price( DigitalCfg( "mcl", K, "call", "asset_or_nothing", 1, 300000 ) );
        CHECK( std::abs( Premium( asset_mr ) - asset_c ) <= 6.0 * Trust( asset_mr ) + 0.1 );
    }

    //! degenerate strikes: a deep-ITM cash-or-nothing call always pays Q -> Q*df; deep-OTM -> 0
    CHECK( Premium( Price( DigitalCfg( "ana", 1.0, "call", "cash_or_nothing", 1, 1 ) ) ) ==
           doctest::Approx( df ).epsilon( 1e-6 ) );
    CHECK( Premium( Price( DigitalCfg( "ana", 1.0e6, "call", "cash_or_nothing", 1, 1 ) ) ) < 1e-9 );
}

// The PDE prices the digital too (same full-domain grid as a vanilla, binary terminal payoff).
// It is grid-accurate AWAY from the strike; exactly at the money the payoff jump sits on the
// spot and the grid smears it (a known digital-PDE effect), so pin agreement off-the-money.
TEST_CASE( "digital: PDE agrees with ANA away from the strike" )
{
    for ( double K : { 70.0, 85.0, 115.0, 130.0 } )
    {
        for ( std::string payout : { std::string( "cash_or_nothing" ), std::string( "asset_or_nothing" ) } )
        {
            CAPTURE( K );
            CAPTURE( payout );
            const double ana = Premium( Price( DigitalCfg( "ana", K, "call", payout, 1, 1 ) ) );
            const double pde = Premium( Price( DigitalCfg( "pde", K, "call", payout, 1, 1 ) ) );
            CHECK( std::abs( pde - ana ) <= 0.02 * ana + 5e-3 );
        }
    }
}
