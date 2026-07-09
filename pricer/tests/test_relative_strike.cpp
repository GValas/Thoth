#include "helpers.hpp"
#include <doctest/doctest.h>
#include <sstream>

using namespace test;

// Relative strikes: is_absolute_strike: false makes the configured strike a
// percent of the underlying's spot at the valuation date. A relative-strike book
// must price exactly like its absolute-strike twin (same cash strike), and the
// resolved cash strike must stay FIXED through the Greek bumps (the bump engine
// mutates the spot; the strike must not follow it).

namespace
{

//! one European vanilla on a spot-150 equity, strike either absolute (cash) or
//! relative (percent of spot), priced by the given engine with premium + delta
std::string RelStrikeCfg( const std::string& method, bool absolute, double strike,
                          const std::string& type, int draws = 40000 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method )
      << " correlation: cor, indicators: [premium, delta, gamma], result: res}\n"
      << CfgBlock( draws, 30, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [6, 6]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 150, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: " << strike
      << ", is_absolute_strike: " << ( absolute ? "true" : "false" )
      << ", maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: european}\n";
    return o.str();
}

} // namespace

// strike 90 (relative, % of the spot 150) == cash strike 135: every engine must
// price the two bookings identically (same paths / grid / closed form), and the
// Greeks must match too — the cash strike is resolved once, so the spot bump
// changes moneyness exactly as it does on the absolute booking.
TEST_CASE( "relative strike prices as its absolute-cash twin on every engine" )
{
    for ( std::string method : { std::string( "ana" ), std::string( "pde" ), std::string( "mcl" ) } )
    {
        for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
        {
            CAPTURE( method );
            CAPTURE( type );

            auto rel = Price( RelStrikeCfg( method, false, 90, type ) );
            auto abs_ = Price( RelStrikeCfg( method, true, 135, type ) );

            //! identical booking -> identical numbers (same engine, same inputs)
            CHECK( Premium( rel ) == doctest::Approx( Premium( abs_ ) ).epsilon( 1e-12 ) );
            CHECK( Greek( rel, "delta" ) == doctest::Approx( Greek( abs_, "delta" ) ).epsilon( 1e-9 ) );
            CHECK( Greek( rel, "gamma" ) == doctest::Approx( Greek( abs_, "gamma" ) ).epsilon( 1e-9 ) );
        }
    }

    //! and the ANA premium is the Black-Scholes price at the resolved cash strike
    double ana = Premium( Price( RelStrikeCfg( "ana", false, 90, "call" ) ) );
    CHECK( std::abs( ana - BsCall( 150, 135, 0.06, 0.30, T1 ) ) <= 1e-2 );
}

// same convention on a barrier's vanilla strike (the barrier level stays absolute)
TEST_CASE( "relative strike on a barrier prices as its absolute twin (ANA)" )
{
    auto cfg = []( bool absolute, double strike )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [6, 6]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
          << "eq: !equity {spot: 150, volatility: vol, currency: eur}\n"
          << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !barrier {underlying: eq, premium_currency: eur, strike: " << strike
          << ", is_absolute_strike: " << ( absolute ? "true" : "false" )
          << ", maturity: 2000-12-31, nominal: 1, type: call, barrier_type: up&out,"
          << " barrier_monitoring_type: continuous_monitoring, barrier_up_level: 200}\n";
        return o.str();
    };
    double rel = Premium( Price( cfg( false, 90 ) ) );
    double abs_ = Premium( Price( cfg( true, 135 ) ) );
    CHECK( rel == doctest::Approx( abs_ ).epsilon( 1e-12 ) );
}

// Basket away-from-the-money: the moment-matched equivalent vol is strike-
// dependent (the shifted-lognormal fit has skew). ANA prices at the strike's
// vol; the PDE grid must diffuse that same strike vol — not the ATM one — to
// agree when component vols are dispersed and the strike is off the money.
// (Regression: with the ATM-vol grid the OTM case below was ~0.11 off ANA.)
TEST_CASE( "basket PDE agrees with ANA away from the money (strike vol)" )
{
    auto cfg = []( const std::string& method, double strike )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
          << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
          << CfgBlock( 60000, 1, 5 )
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [4, 4]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "cor: !correlation_matrix {underlyings: [eq1, eq2], matrix: [1, 0.8, 0.8, 1]}\n"
          << "eq1: !equity {spot: 170, volatility: vol1, currency: eur}\n"
          << "eq2: !equity {spot: 190, volatility: vol2, currency: eur}\n"
          << "vol1: !bs_volatility {volatility: 13, calendar: cal}\n"
          << "vol2: !bs_volatility {volatility: 48, calendar: cal}\n"
          << "bk: !basket {underlyings: [eq1, eq2], weights: [0.65, 0.35]}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: bk, premium_currency: eur, strike: " << strike
          << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1,"
          << " type: call, exercise: european}\n";
        return o.str();
    };

    //! basket spot is the rebased 100; test an ITM and an OTM strike
    for ( double K : { 81.0, 119.0 } )
    {
        CAPTURE( K );
        double ana = Premium( Price( cfg( "ana", K ) ) );
        double pde = Premium( Price( cfg( "pde", K ) ) );
        CHECK( std::abs( ana - pde ) <= 0.06 ); //!< grid error only, no vol mismatch
    }
}
