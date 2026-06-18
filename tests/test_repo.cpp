#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Repo (borrow/securities-lending spread) is a continuous carry yield subtracted
// from the rate, exactly like a continuous dividend: the forward becomes
// F = S e^{(r - repo) T}. It must enter all three engines consistently — the ANA
// forward, the PDE carry and the MCL drift — so a repo-bearing equity prices the
// same across ana / pde / mcl.

namespace
{
//! a European vanilla on one equity, optionally carrying a flat repo spread
//! (repo_pct in percent; <= 0 -> no repo curve).
std::string RepoCfg( const std::string& method, const std::string& type,
                     double repo_pct, int draws )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( method, draws, 30, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n";
    if ( repo_pct > 0 )
    {
        o << "repo: !repo_curve {dates: [2000-01-01, 2010-01-01], values: ["
          << repo_pct << ", " << repo_pct << "]}\n";
    }
    o << "eq: !equity {spot: 100, volatility: vol, currency: eur"
      << ( repo_pct > 0 ? ", repo: repo" : "" ) << "}\n"
      << "vol: !bs_volatility {volatility: 20, calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: european}\n";
    return o.str();
}
} // namespace

TEST_CASE( "repo enters the forward analytically (F = S e^{(r-repo)T})" )
{
    const double S = 100, K = 100, r = 0.05, vol = 0.20, repo = 0.03;
    //! repo is a carry yield: equivalent to pricing on S*e^{-repo*T}
    for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
    {
        CAPTURE( type );
        const double ana = Premium( Price( RepoCfg( "ana", type, repo * 100, 1 ) ) );
        const double ref = ( type == "call" ) ? BsCall( S * std::exp( -repo * T1 ), K, r, vol, T1 )
                                              : BsPut( S * std::exp( -repo * T1 ), K, r, vol, T1 );
        CHECK( ana == doctest::Approx( ref ).epsilon( 1e-6 ) );
    }
}

TEST_CASE( "repo lowers the call and raises the put (forward drops)" )
{
    const double call0 = Premium( Price( RepoCfg( "ana", "call", 0, 1 ) ) );
    const double call3 = Premium( Price( RepoCfg( "ana", "call", 3, 1 ) ) );
    const double put0 = Premium( Price( RepoCfg( "ana", "put", 0, 1 ) ) );
    const double put3 = Premium( Price( RepoCfg( "ana", "put", 3, 1 ) ) );
    CHECK( call3 < call0 );
    CHECK( put3 > put0 );
}

TEST_CASE( "repo is carried consistently across ANA, PDE and MCL" )
{
    const double ana = Premium( Price( RepoCfg( "ana", "call", 3, 1 ) ) );
    const double pde = Premium( Price( RepoCfg( "pde", "call", 3, 1 ) ) );

    auto mcl_res = Price( RepoCfg( "mcl", "call", 3, 200000 ) );
    const double mcl = Premium( mcl_res );

    CAPTURE( ana );
    CAPTURE( pde );
    CAPTURE( mcl );
    CHECK( pde == doctest::Approx( ana ).epsilon( 0.01 ) );          //!< PDE grid vs analytic
    CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mcl_res ) + 1e-2 ); //!< MC within error
}
