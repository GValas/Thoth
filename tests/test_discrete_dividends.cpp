#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Discrete cash dividends, escrowed-dividend model: the forward (and the MCL
// diffusion spot) net the present value of the dividends due before maturity off
// the spot. With a single dividend paid ON the maturity date its PV is exactly
// amount * DF(T), so the escrowed spot is S - amount*DF(T) and the analytic price
// must equal Black-Scholes on that escrowed spot. ANA / PDE / MCL all read the
// same escrowed forward, so they must agree.

namespace
{
//! a European vanilla on one equity, optionally carrying a single cash dividend
//! paid on the maturity date (amount <= 0 -> no dividend).
std::string DivCfg( const std::string& method, const std::string& type,
                    double amount, int draws )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( method, draws, 30, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur"
      << ( amount > 0 ? ", discrete_dividends: divs" : "" ) << "}\n";
    if ( amount > 0 )
    {
        o << "divs: !discrete_dividends {dates: [2000-12-31], amounts: [" << amount << "]}\n";
    }
    o << "vol: !bs_volatility {volatility: 20, calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: european}\n";
    return o.str();
}
} // namespace

TEST_CASE( "discrete-dividend ANA equals Black-Scholes on the escrowed spot" )
{
    const double S = 100, K = 100, r = 0.05, vol = 0.20, amount = 5.0;
    //! dividend paid at maturity -> PV = amount * DF(T)
    const double pv = amount * std::exp( -r * T1 );

    for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
    {
        CAPTURE( type );
        const double ana = Premium( Price( DivCfg( "ana", type, amount, 1 ) ) );
        const double ref = ( type == "call" ) ? BsCall( S - pv, K, r, vol, T1 )
                                              : BsPut( S - pv, K, r, vol, T1 );
        CHECK( ana == doctest::Approx( ref ).epsilon( 1e-6 ) );
    }
}

TEST_CASE( "a dividend lowers the call and raises the put (forward drops)" )
{
    const double call0 = Premium( Price( DivCfg( "ana", "call", 0, 1 ) ) );
    const double call5 = Premium( Price( DivCfg( "ana", "call", 5, 1 ) ) );
    const double put0 = Premium( Price( DivCfg( "ana", "put", 0, 1 ) ) );
    const double put5 = Premium( Price( DivCfg( "ana", "put", 5, 1 ) ) );
    CHECK( call5 < call0 );
    CHECK( put5 > put0 );
}

TEST_CASE( "discrete-dividend escrow is consistent across ANA, PDE and MCL" )
{
    const double ana = Premium( Price( DivCfg( "ana", "call", 5, 1 ) ) );
    const double pde = Premium( Price( DivCfg( "pde", "call", 5, 1 ) ) );

    auto mcl_res = Price( DivCfg( "mcl", "call", 5, 200000 ) );
    const double mcl = Premium( mcl_res );

    CAPTURE( ana );
    CAPTURE( pde );
    CAPTURE( mcl );
    CHECK( pde == doctest::Approx( ana ).epsilon( 0.01 ) );          //!< PDE grid vs analytic
    CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mcl_res ) + 1e-2 ); //!< MC within error
}
