#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Multi-curve / OIS discounting: a !currency may name a separate `discount_rate`
// curve. Forwards and drifts stay on the projection curve (`rate`); cash flows
// are discounted on the OIS curve — in every engine (ANA df, PDE per-step
// discount rates, MCL ContractNode + LSM discounting). Without `discount_rate`
// everything reduces to the historic single-curve behaviour (same curve object).

namespace
{

//! one European/American call on an equity funding at r_proj, discounted at r_ois
std::string MultiCurveCfg( const std::string& method, double r_proj_pct,
                           double r_ois_pct, const std::string& exercise,
                           int draws = 1, int pde_precision = 5 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium, rho], result: res}\n"
      << CfgBlock( draws, 30, pde_precision )
      << "eur: !currency {rate: proj_curve, discount_rate: ois_curve}\n"
      << "proj_curve: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << r_proj_pct << ", " << r_proj_pct << "]}\n"
      << "ois_curve: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << r_ois_pct << ", " << r_ois_pct << "]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call"
      << ", exercise: " << exercise << "}\n";
    return o.str();
}

//! Black on a forward: df * (F N(d1) - K N(d2))
double Black( double F, double K, double df, double sig, double T )
{
    const double sd = sig * std::sqrt( T );
    const double d1 = std::log( F / K ) / sd + sd / 2;
    return df * ( F * NormCdf( d1 ) - K * NormCdf( d1 - sd ) );
}

} // namespace

// European call, funding 8% / OIS 5%: the forward grows at the PROJECTION rate,
// the premium discounts on the OIS curve — closed form Black(F = S e^{r_p T},
// df = e^{-r_ois T}). All three engines must agree with it.
TEST_CASE( "multi-curve: European vanilla discounts on the OIS curve in all engines" )
{
    const double r_p = 0.08, r_o = 0.05;
    const double ref = Black( 100 * std::exp( r_p * T1 ), 100,
                              std::exp( -r_o * T1 ), 0.30, T1 );

    double ana = Premium( Price( MultiCurveCfg( "ana", 100 * r_p, 100 * r_o, "european" ) ) );
    double pde = Premium( Price( MultiCurveCfg( "pde", 100 * r_p, 100 * r_o, "european" ) ) );
    auto mr = Price( MultiCurveCfg( "mcl", 100 * r_p, 100 * r_o, "european", 80000 ) );
    double mcl = Premium( mr );

    CHECK( std::abs( ana - ref ) <= 1e-2 );                     //!< closed form
    CHECK( std::abs( pde - ref ) <= 0.05 );                     //!< grid discretisation
    CHECK( std::abs( mcl - ref ) <= 6.0 * Trust( mr ) + 1e-2 ); //!< MC error

    //! sanity: the single-curve prices differ from the multi-curve one on BOTH
    //! sides (discounting at r_p or growing at r_o would each be wrong)
    const double single_rp = Black( 100 * std::exp( r_p * T1 ), 100,
                                    std::exp( -r_p * T1 ), 0.30, T1 );
    const double single_ro = Black( 100 * std::exp( r_o * T1 ), 100,
                                    std::exp( -r_o * T1 ), 0.30, T1 );
    CHECK( std::abs( ana - single_rp ) > 0.2 );
    CHECK( std::abs( ana - single_ro ) > 0.2 );
}

// rho bumps the projection AND the OIS curve in parallel: the engine Greek must
// match the symmetric reprice of the closed form under a joint 1bp-style shift
// (the engine uses a one-sided GREEK_RATE_BUMP = 1e-4 bump, scaled per 1%).
TEST_CASE( "multi-curve: rho bumps both curves" )
{
    const double r_p = 0.08, r_o = 0.05, h = 1e-4;
    auto price = [&]( double shift )
    {
        return Black( 100 * std::exp( ( r_p + shift ) * T1 ), 100,
                      std::exp( -( r_o + shift ) * T1 ), 0.30, T1 );
    };
    const double ref_rho = ( price( h ) - price( 0 ) ) / h * 0.01;

    auto r = Price( MultiCurveCfg( "ana", 100 * r_p, 100 * r_o, "european" ) );
    CHECK( std::abs( Greek( r, "rho" ) - ref_rho ) <= 1e-3 );
}

// American exercise: the PDE backward induction and the Longstaff-Schwartz MC
// both discount interim cashflows on the OIS curve while the spot drifts at the
// projection carry — the two engines must agree (LSM is a slightly low-biased
// lower bound, hence the one-sided cushion).
TEST_CASE( "multi-curve: American PDE and LSM agree" )
{
    //! a put makes early exercise valuable (call on a no-dividend stock is European)
    auto put_cfg = []( const std::string& method, int draws )
    {
        std::string cfg = MultiCurveCfg( method, 8, 5, "american", draws );
        const std::string from = "type: call", to = "type: put";
        cfg.replace( cfg.find( from ), from.size(), to );
        return cfg;
    };
    double pde = Premium( Price( put_cfg( "pde", 1 ) ) );
    auto mr = Price( put_cfg( "mcl", 80000 ) );
    double mcl = Premium( mr );
    CHECK( std::abs( mcl - pde ) <= 6.0 * Trust( mr ) + 0.08 );
}

// Omitting discount_rate must reproduce the single-curve price exactly.
TEST_CASE( "multi-curve: default (no discount_rate) is the historic single curve" )
{
    std::string cfg = MultiCurveCfg( "ana", 8, 8, "european" );
    const std::string from = ", discount_rate: ois_curve", to = "";
    std::string single = cfg;
    single.replace( single.find( from ), from.size(), to );

    //! same flat 8% on both fields == the field omitted
    CHECK( Premium( Price( cfg ) ) == doctest::Approx( Premium( Price( single ) ) ).epsilon( 1e-12 ) );
}
