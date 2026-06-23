#include "helpers.hpp"
#include <doctest/doctest.h>
#include <vector>

using namespace test;

namespace
{
struct AssetSpec
{
    std::string name;
    double spot, vol;
};

//! a 3-asset correlated book with one call and one put per underlying
std::string MultiAssetCfg( int draws )
{
    std::vector<AssetSpec> a = { { "eq_a", 100, 30 }, { "eq_b", 85, 25 }, { "eq_c", 120, 35 } };
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " mcl_configuration: cfg_mcl, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( draws, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq_a, eq_b, eq_c],"
      << " matrix: [1, 0.3, 0.2, 0.3, 1, 0.1, 0.2, 0.1, 1]}\n";
    std::ostringstream opts;
    for ( const AssetSpec& x : a )
    {
        o << x.name << ": !equity {spot: " << x.spot << ", volatility: " << x.name
          << "_v, currency: eur}\n"
          << x.name << "_v: !bs_volatility {volatility: " << x.vol
          << ", calendar: cal}\n";
        for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
        {
            std::string id = x.name + "_" + type;
            o << id << ": !vanilla {underlying: " << x.name
              << ", premium_currency: eur, strike: " << x.spot
              << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
              << ", exercise: european}\n";
            opts << ( opts.tellp() ? ", " : "" ) << id;
        }
    }
    o << "book: !book {contracts: [" << opts.str() << "]}\n";
    return o.str();
}
} // namespace

TEST_CASE( "multi-asset book: each option matches its own-spot Black-Scholes" )
{
    // Regression guard for (a) the per-contract premium reporting (spot != 100)
    // and (b) marginal volatility preservation under correlated diffusion.
    const double r = 0.08;
    std::vector<AssetSpec> a = { { "eq_a", 100, 30 }, { "eq_b", 85, 25 }, { "eq_c", 120, 35 } };

    auto res = Price( MultiAssetCfg( 200000 ) );

    double sum_mc = 0, sum_bs = 0;
    for ( const AssetSpec& x : a )
    {
        for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
        {
            std::string id = x.name + "_" + type;
            CAPTURE( id );
            double mc = Premium( res, id );
            double trust = Trust( res, id );
            double bs = ( type == "call" )
                            ? BsCall( x.spot, x.spot, r, x.vol / 100, T1 )
                            : BsPut( x.spot, x.spot, r, x.vol / 100, T1 );
            CHECK( std::abs( mc - bs ) <= 6.0 * trust + 1e-3 );
            sum_mc += mc;
            sum_bs += bs;
        }
    }

    // the book total is the sum of the contract premiums
    CHECK( Premium( res ) == doctest::Approx( sum_mc ).epsilon( 1e-6 ) );
    // and is close to the analytic sum
    CHECK( std::abs( sum_mc - sum_bs ) <= 0.01 * sum_bs );
}
