#include "helpers.hpp"
#include "single.hpp"
#include "currency.hpp"
#include "yield_curve.hpp"
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
                        double notional, const std::string& method,
                        int obs_days = 0, int draws = 1 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( draws, 30, 5 )
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
      << ", notional: " << notional;
    if ( obs_days > 0 )
    {
        o << ", observation_period_days: " << obs_days;
    }
    o << "}\n";
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

// --- discrete observation ----------------------------------------------------
//
// A discretely-observed swap samples the realized variance on its fixing schedule
// (today + k*period up to maturity) instead of every diffusion step. Per interval
// E[(log S_{t2}/S_{t1})^2] = sigma^2*dt + mean^2 with mean = (r - sigma^2/2)*dt
// under flat BS, so the fair variance gains the deterministic Sum(mean_i^2)/T on
// top of the continuous sigma^2. The MCL path sampling produces the term
// naturally; ANA and PDE add it via VarianceSwap::ObservationDriftVariance.
TEST_CASE( "discretely-observed variance swap adds the drift^2 term (3 engines)" )
{
    //! high carry + low vol makes the drift^2 term material: (r - sigma^2/2) =
    //! 0.095, monthly fixings on a 1y swap -> ~7% of the continuous fair variance
    const double vol = 0.10, r = 0.10, notl = 10000;
    const int obs = 30; //!< monthly fixings

    //! reference add-on, replicating the engine's schedule convention exactly:
    //! fixings at today + 30k (k>=1, < maturity), plus maturity (365d, leap 2000)
    const double b = r - 0.5 * vol * vol;
    double sum_m2 = 0;
    int prev = 0;
    for ( int d = obs; d < 365; d += obs )
    {
        const double dt = ( d - prev ) / 365.0;
        sum_m2 += ( b * dt ) * ( b * dt );
        prev = d;
    }
    const double dt_last = ( 365 - prev ) / 365.0; //!< stub interval to maturity
    sum_m2 += ( b * dt_last ) * ( b * dt_last );
    const double addon_pv = notl * std::exp( -r * T1 ) * sum_m2 / T1;

    //! ANA / PDE: the discrete-minus-continuous DIFFERENCE isolates the drift^2
    //! add-on (the shared strip / grid error cancels in the difference)
    const double ana_c = Premium( Price( VarSwapCfg( vol * 100, r * 100, 0, notl, "ana" ) ) );
    const double ana_d = Premium( Price( VarSwapCfg( vol * 100, r * 100, 0, notl, "ana", obs ) ) );
    CAPTURE( ana_c );
    CAPTURE( ana_d );
    CAPTURE( addon_pv );
    CHECK( ana_d - ana_c == doctest::Approx( addon_pv ).epsilon( 0.01 ) );

    const double pde_c = Premium( Price( VarSwapCfg( vol * 100, r * 100, 0, notl, "pde" ) ) );
    const double pde_d = Premium( Price( VarSwapCfg( vol * 100, r * 100, 0, notl, "pde", obs ) ) );
    CAPTURE( pde_c );
    CAPTURE( pde_d );
    CHECK( pde_d - pde_c == doctest::Approx( addon_pv ).epsilon( 0.01 ) );

    //! MCL prices the discrete swap directly off the sampled path (fixing dates are
    //! forced into the diffusion grid); it must agree with the corrected ANA value
    auto mr = Price( VarSwapCfg( vol * 100, r * 100, 0, notl, "mcl", obs, 200000 ) );
    const double mcl_d = Premium( mr );
    CAPTURE( mcl_d );
    CHECK( std::abs( mcl_d - ana_d ) <= 6.0 * Trust( mr ) + 0.01 * ana_d );
}

TEST_CASE( "daily observation converges to the continuous variance swap" )
{
    //! with daily fixings the drift^2 add-on is (b^2/365)/T of variance — a few
    //! bp of the continuous value — so the discrete swap sits on top of it
    const double vol = 0.30, r = 0.05, notl = 10000;
    const double cont = Premium( Price( VarSwapCfg( vol * 100, r * 100, 0, notl, "ana" ) ) );
    const double daily = Premium( Price( VarSwapCfg( vol * 100, r * 100, 0, notl, "ana", 1 ) ) );
    CAPTURE( cont );
    CAPTURE( daily );
    CHECK( daily == doctest::Approx( cont ).epsilon( 0.001 ) );
    CHECK( daily >= cont ); //!< the drift^2 add-on is non-negative
}

// --- discrete observation under a TERM-STRUCTURED surface ---------------------
//
// The drift^2 add-on uses each interval's FORWARD ATM implied variance
// (sigma^2(t2) t2 - sigma^2(t1) t1), not a single maturity-ATM vol: on a sloped
// term structure the early intervals carry the short vol and the late ones the
// forward vol. The oracle below replicates the convention off the engine's OWN
// implied surface (queried directly), so the test pins the schedule/variance
// convention without re-deriving Hagan.
TEST_CASE( "discrete variance swap uses per-interval forward variance on a term structure" )
{
    const double notl = 10000, r = 0.10;
    const int obs = 30;

    //! steep upward alpha term structure (15% -> 35%), no smile (beta 1, rho 0,
    //! tiny nu) so the surface is a pure term structure of ATM vols
    auto cfg = [&]( const std::string& method, int obs_days )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
          << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
          << CfgBlock( 1, 30, 5 )
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [" << r * 100 << ", " << r * 100 << "]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << "vol: !sabr_volatility {maturities: [0.25, 1.0], alpha: [0.15, 0.35],"
          << " beta: [1.0, 1.0], rho: [0, 0], nu: [0.0001, 0.0001], calendar: cal}\n"
          << "book: !book {contracts: [vs]}\n"
          << "vs: !variance_swap {underlying: eq, premium_currency: eur,"
          << " maturity: 2000-12-31, volatility_strike: 0, notional: " << notl;
        if ( obs_days > 0 )
        {
            o << ", observation_period_days: " << obs_days;
        }
        o << "}\n";
        return o.str();
    };

    //! discrete-minus-continuous ANA difference isolates the drift^2 add-on
    const double ana_c = Premium( Price( cfg( "ana", 0 ) ) );
    const double ana_d = Premium( Price( cfg( "ana", obs ) ) );

    //! oracle: the same schedule and forward-variance convention, evaluated on the
    //! engine's own surface via direct object access (run once to cascade today)
    std::streambuf* saved = std::cout.rdbuf();
    std::ostringstream sink;
    std::cout.rdbuf( sink.rdbuf() );
    ObjectManager manager( YamlConfig::from_string_t{}, cfg( "ana", obs ) );
    manager.ReadObjects( ROOT_NODE );
    manager.ExecuteTask();
    std::cout.rdbuf( saved );
    auto* eq = manager.collector().Get<Single>( "eq" );
    auto* ccy = manager.collector().Get<Currency>( "eur" );
    REQUIRE( eq != nullptr );
    REQUIRE( ccy != nullptr );

    const date today( 2000, 1, 1 ), maturity( 2000, 12, 31 );
    const double T = YearFraction( today, maturity );
    std::set<date> schedule;
    for ( date d = today + days( obs ); d < maturity; d += days( obs ) )
    {
        schedule.insert( d );
    }
    schedule.insert( maturity );

    double sum_m2 = 0, f1 = eq->GetForward( today, ccy ), cum1 = 0;
    for ( const date& d : schedule )
    {
        const double f2 = eq->GetForward( d, ccy );
        const double t2 = YearFraction( today, d );
        const double atm = eq->GetImplicitVol( f2, d );
        const double cum2 = atm * atm * t2;
        const double m = std::log( f2 / f1 ) - 0.5 * std::max( 0.0, cum2 - cum1 );
        sum_m2 += m * m;
        f1 = f2;
        cum1 = cum2;
    }
    const double df = ccy->GetRate()->GetDiscountFactor( maturity );
    const double addon_pv = notl * df * sum_m2 / T;

    CAPTURE( ana_c );
    CAPTURE( ana_d );
    CAPTURE( addon_pv );
    CHECK( ana_d - ana_c == doctest::Approx( addon_pv ).epsilon( 0.01 ) );

    //! sanity: a single maturity-ATM vol (the OLD convention) yields a visibly
    //! different add-on on this steep term structure — the test discriminates
    const double atm_mat = eq->GetImplicitVol( eq->GetForward( maturity, ccy ), maturity );
    double sum_old = 0;
    double f1o = eq->GetForward( today, ccy );
    date d1 = today;
    for ( const date& d : schedule )
    {
        const double f2 = eq->GetForward( d, ccy );
        const double dt = YearFraction( d1, d );
        const double m = std::log( f2 / f1o ) - 0.5 * atm_mat * atm_mat * dt;
        sum_old += m * m;
        d1 = d;
        f1o = f2;
    }
    const double addon_old = notl * df * sum_old / T;
    CAPTURE( addon_old );
    CHECK( std::abs( addon_pv - addon_old ) > 0.02 * addon_pv );
}
