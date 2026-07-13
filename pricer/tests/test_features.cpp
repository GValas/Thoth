#include "helpers.hpp"
#include "single.hpp"     //!< direct surface queries (SABR wing tests)
#include "volatility.hpp" //!< direct surface queries (SABR wing tests)
#include <doctest/doctest.h>

// Coverage for features the rest of the suite did not exercise: variance swap,
// Sobol QMC paths, the !sequence task, baskets, composites and the SABR surface.
// Assertions favour cross-engine agreement / parity / determinism / sanity
// bounds over brittle reference numbers.

using namespace test;

namespace
{
//! a one-pricer book on a single eur equity, parameterised by the contract block
//! and the pricing method. draws/step matter only for mcl.
std::string OneContract( const std::string& method, const std::string& contract_block,
                         const std::string& extra_objects = "", int draws = 200000,
                         int step = 7 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( draws, step, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << extra_objects
      << "book: !book {contracts: [o]}\n"
      << contract_block;
    return o.str();
}
} // namespace

// --- calendar day-weight consistency : with a non-unit non_working_days_weight,
// the vol is scaled by GetDayWeight() in ANA/PDE; the MCL diffusion must apply the
// same scaling. Before the fix, MCL ignored the day-weight and diverged. A vanilla
// priced ANA vs MCL under weight=2 must agree (both use the weighted vol).
TEST_CASE( "calendar day-weight: MCL matches ANA under a non-unit weight" )
{
    const std::string c = "o: !vanilla {underlying: eq, premium_currency: eur,"
                          " strike: 100, maturity: 2000-12-31, type: call, exercise: european}\n";
    // override the default weight-1 calendar with weight 2 (dayweight = sqrt(9/7))
    auto with_weight = []( std::string cfg )
    {
        const std::string from = "non_working_days_weight: 1";
        const std::string to = "non_working_days_weight: 2";
        cfg.replace( cfg.find( from ), from.size(), to );
        return cfg;
    };
    double ana = Premium( Price( with_weight( OneContract( "ana", c ) ) ) );
    auto mr = Price( with_weight( OneContract( "mcl", c ) ) );
    double mcl = Premium( mr );
    CAPTURE( ana );
    CAPTURE( mcl );
    CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mr ) + 5e-2 );
}

// --- variance swap : the Monte-Carlo realized variance and the analytic static
// replication must agree, and the fair value is positive for a strike below vol.
TEST_CASE( "variance swap: MCL and ANA agree" )
{
    const std::string c = "o: !variance {underlying: eq, premium_currency: eur,"
                          " maturity: 2000-12-31, volatility_strike: 20, notional: 10000}\n";
    double ana = Premium( Price( OneContract( "ana", c ) ) );
    auto mr = Price( OneContract( "mcl", c ) );
    double mcl = Premium( mr );

    CHECK( ana > 0.0 );                                        //!< 30% realized vs 20% strike -> positive
    CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mr ) + 2.0 ); //!< engines agree (MC error)
}

// --- Sobol QMC : a use_sobol MCL run prices a vanilla, is deterministic across
// runs, and lands within MC error of Black-Scholes (exercises path_generator /
// sobol_generator).
TEST_CASE( "Sobol MCL: deterministic and converges to Black-Scholes" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " mcl_configuration: m, correlation: cor, indicators: [premium], result: res}\n"
      << "m: !mcl_configuration {max_day_step: 30, min_day_step: -1, paths: 100000,"
      << " vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [6, 6]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    double a = Premium( Price( o.str() ) );
    double b = Premium( Price( o.str() ) );
    CHECK( a == doctest::Approx( b ).epsilon( 1e-12 ) );                //!< deterministic
    CHECK( std::abs( a - BsCall( 100, 100, 0.06, 0.30, T1 ) ) <= 0.2 ); //!< QMC accuracy
}

// --- !sequence : runs each sub-task in order, each writing its own result block.
TEST_CASE( "sequence runs every sub-task" )
{
    std::ostringstream o;
    o << "root: seq\n"
      << "seq: !sequence {tasks: [p_pde, p_ana], result: seq_result}\n"
      << "p_pde: !pde_pricer {today: 2000-01-01, book: book, currency: eur, pde_configuration: pcfg,"
      << " correlation: cor, indicators: [premium], result: pde_res}\n"
      << "p_ana: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " correlation: cor, indicators: [premium], result: ana_res}\n"
      << "pcfg: !pde_configuration {vanilla_precision: high}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    YAML::Node r = Price( o.str(), "seq" );
    double pde = r["pde_res"]["premium"].as<double>();
    double ana = r["ana_res"]["premium"].as<double>();
    CHECK( pde == doctest::Approx( ana ).epsilon( 0.01 ) ); //!< both ran, agree on the same book
}

// --- basket : with near-perfect correlation a 50/50 basket of two identical
// equities is (almost) a single-asset option (diversification ~vanishes).
TEST_CASE( "basket: near-perfectly correlated 50/50 basket equals the single asset" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur, mcl_configuration: m,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "m: !mcl_configuration {max_day_step: 30, min_day_step: -1, paths: 60000,"
      << " vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq1: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "eq2: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq1, eq2], matrix: [1, 0.99, 0.99, 1]}\n"
      << "bk: !basket {underlyings: [eq1, eq2], weights: [0.5, 0.5]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: bk, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    double basket = Premium( Price( o.str() ) );
    CHECK( std::abs( basket - BsCall( 100, 100, 0.05, 0.30, T1 ) ) <= 0.3 );
}

// --- composite : an asset quoted in EUR but settled in USD at the prevailing
// FX. The USD value S*FX is lognormal, so the composite is a plain BS asset in
// USD with composite spot = S0*FX0, forward = spot*exp(r_usd*T) and composite
// vol^2 = vol_S^2 + vol_FX^2 + 2*rho*vol_S*vol_FX. The three engines must agree
// with each other and with that closed form.
TEST_CASE( "composite underlying matches the closed form across ANA/MCL/PDE" )
{
    const double S = 100, fx = 1.5, vol_s = 0.30, vol_fx = 0.15, rho = 0.5;
    const double r_usd = 0.05, K = 150;

    // closed-form composite Black-Scholes reference (lognormal USD value)
    const double s_comp = S * fx;
    const double v_comp = std::sqrt( vol_s * vol_s + vol_fx * vol_fx +
                                     2 * rho * vol_s * vol_fx );
    const double ref = BsCall( s_comp, K, r_usd, v_comp, T1 );

    auto cfg = []( const std::string& method )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: usd," << ( method == "ana" ? "" : method == "pde" ? " pde_configuration: pd,"
                                                                                                                                           : " mcl_configuration: m," )
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "m: !mcl_configuration {max_day_step: 7, min_day_step: -1, paths: 200000,"
          << " vol_year_step: 0.01, use_sobol: true}\n"
          << "pd: !pde_configuration {vanilla_precision: high}\n"
          << "eur: !currency {rate: r_eur}\n"
          << "r_eur: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
          << "usd: !currency {rate: r_usd}\n"
          << "r_usd: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
          << "eur/usd: !forex {base_currency: usd, underlying_currency: eur, spot: 1.5, volatility: fxvol}\n"
          << "fxvol: !bs_volatility {volatility: 15}\n"
          << "comp: !composite {equity: eq, composite_currency: usd}\n"
          << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd], matrix: [1, 0.5, 0.5, 1]}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: comp, premium_currency: usd, strike: 150,"
          << " maturity: 2000-12-31, type: call, exercise: european}\n";
        return o.str();
    };

    auto ana = Price( cfg( "ana" ) );
    auto mcl = Price( cfg( "mcl" ) );
    auto pde = Price( cfg( "pde" ) );
    const double p_ana = Premium( ana ), p_mcl = Premium( mcl ), p_pde = Premium( pde );
    CAPTURE( ref );
    CAPTURE( p_ana );
    CAPTURE( p_mcl );
    CAPTURE( p_pde );

    // each engine matches the closed form (ANA exact; PDE grid; MCL within noise)
    CHECK( std::abs( p_ana - ref ) <= 1e-2 );
    CHECK( std::abs( p_pde - ref ) <= 0.1 );
    CHECK( std::abs( p_mcl - ref ) <= 6.0 * Trust( mcl ) + 5e-2 );

    // and the engines agree with each other
    CHECK( std::abs( p_ana - p_pde ) <= 0.1 );
    CHECK( std::abs( p_ana - p_mcl ) <= 6.0 * Trust( mcl ) + 5e-2 );
}

// --- CROSS-currency composite: a EUR asset composited into JPY, where the market only
// stores the usd-pivot FX basis (usd/eur, usd/jpy) — the eur/jpy conversion is triangulated.
// The MCL leg builds the cross FX from the two pivot legs as a ratio of lognormals, whose
// convexity is removed by a deterministic exp(-(sigma_PA^2 - rho sigma_PA sigma_PB) t) factor
// (Correlation::GetFxNode) so the composite forward matches the single-lognormal one ANA/PDE
// use. Regression: without the convexity correction MCL over-forwards ~0.4% and disagrees.
TEST_CASE( "cross-currency composite (EUR asset, JPY payoff) agrees across ANA/MCL/PDE" )
{
    auto cfg = []( const std::string& method, double strike, int draws )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: jpy,"
          << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
          << CfgBlock( draws, 5, 5 )
          << "usd: !currency {rate: r_usd}\n"
          << "r_usd: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "eur: !currency {rate: r_eur}\n"
          << "r_eur: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
          << "jpy: !currency {rate: r_jpy}\n"
          << "r_jpy: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [1, 1]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
          << "usd/eur: !forex {base_currency: eur, underlying_currency: usd, spot: 1.1, volatility: fx_ue}\n"
          << "fx_ue: !bs_volatility {volatility: 10, calendar: cal}\n"
          << "usd/jpy: !forex {base_currency: jpy, underlying_currency: usd, spot: 150, volatility: fx_uj}\n"
          << "fx_uj: !bs_volatility {volatility: 12, calendar: cal}\n"
          << "comp: !composite {equity: eq, composite_currency: jpy}\n"
          << "cor: !correlation_matrix {underlyings: [eq], forexs: [usd/eur, usd/jpy],"
          << " matrix: [1, 0.3, 0.2, 0.3, 1, 0.5, 0.2, 0.5, 1]}\n"
          << "o: !vanilla {underlying: comp, premium_currency: jpy, strike: " << strike
          << ", is_absolute_strike: true, maturity: 2000-12-31, type: call, exercise: european}\n"
          << "book: !book {contracts: [o]}\n";
        return o.str();
    };

    for ( double strike : { 12000.0, 13773.0 } ) // composite spot ~= 100*150/1.1 = 13636
    {
        CAPTURE( strike );
        const double ana = Premium( Price( cfg( "ana", strike, 1 ) ) );
        const double pde = Premium( Price( cfg( "pde", strike, 1 ) ) );
        auto mr = Price( cfg( "mcl", strike, 400000 ) );
        const double mcl = Premium( mr );
        CAPTURE( ana );
        CAPTURE( pde );
        CAPTURE( mcl );
        CHECK( ana > 0 );
        CHECK( std::abs( pde - ana ) <= 0.01 * ana + 0.05 );                     //!< closed form vs grid
        CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mr ) + 0.01 * ana + 0.05 ); //!< MC error (convexity fixed)
    }
}

// --- composite on a dividend/repo-paying asset : the USD value S*FX is a traded
// USD asset paying the wrapped equity's continuous carry q (= dividend yield +
// repo), so under the USD measure it drifts at (r_usd - q) and the composite
// forward is s_comp*exp((r_usd-q)T). The MCL leg diffuses the wrapped equity with
// its full r-q-repo drift; ANA/PDE must apply the same carry (Composite::GetForward),
// else they drop exp(-qT) and disagree. This case fails without that carry factor.
TEST_CASE( "composite with dividend + repo carry agrees across ANA/MCL/PDE" )
{
    const double S = 100, fx = 1.5, vol_s = 0.30, vol_fx = 0.15, rho = 0.5;
    const double r_usd = 0.05, K = 150;
    const double q = 0.03 + 0.02; //!< continuous dividend yield 3% + repo 2%

    // closed-form composite BS with carry: forward = s_comp*exp((r_usd-q)T), which
    // equals BS on a carry-discounted spot s_comp*exp(-qT) grown at r_usd.
    const double s_comp = S * fx;
    const double v_comp = std::sqrt( vol_s * vol_s + vol_fx * vol_fx +
                                     2 * rho * vol_s * vol_fx );
    const double ref = BsCall( s_comp * std::exp( -q * T1 ), K, r_usd, v_comp, T1 );

    auto cfg = []( const std::string& method )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: usd," << ( method == "ana" ? "" : method == "pde" ? " pde_configuration: pd,"
                                                                                                                                           : " mcl_configuration: m," )
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "m: !mcl_configuration {max_day_step: 7, min_day_step: -1, paths: 200000,"
          << " vol_year_step: 0.01, use_sobol: true}\n"
          << "pd: !pde_configuration {vanilla_precision: high}\n"
          << "eur: !currency {rate: r_eur}\n"
          << "r_eur: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
          << "usd: !currency {rate: r_usd}\n"
          << "r_usd: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "div: !continuous_dividends_curve {dates: [2000-01-01, 2010-01-01], values: [3, 3]}\n"
          << "rep: !repo_curve {dates: [2000-01-01, 2010-01-01], values: [2, 2]}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur,"
          << " continuous_dividends: div, repo: rep}\n"
          << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
          << "eur/usd: !forex {base_currency: usd, underlying_currency: eur, spot: 1.5, volatility: fxvol}\n"
          << "fxvol: !bs_volatility {volatility: 15}\n"
          << "comp: !composite {equity: eq, composite_currency: usd}\n"
          << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd], matrix: [1, 0.5, 0.5, 1]}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: comp, premium_currency: usd, strike: 150,"
          << " maturity: 2000-12-31, type: call, exercise: european}\n";
        return o.str();
    };

    auto ana = Price( cfg( "ana" ) );
    auto mcl = Price( cfg( "mcl" ) );
    auto pde = Price( cfg( "pde" ) );
    const double p_ana = Premium( ana ), p_mcl = Premium( mcl ), p_pde = Premium( pde );
    CAPTURE( ref );
    CAPTURE( p_ana );
    CAPTURE( p_mcl );
    CAPTURE( p_pde );

    // each engine matches the carry-adjusted closed form
    CHECK( std::abs( p_ana - ref ) <= 1e-2 );
    CHECK( std::abs( p_pde - ref ) <= 0.1 );
    CHECK( std::abs( p_mcl - ref ) <= 6.0 * Trust( mcl ) + 5e-2 );

    // and the engines agree with each other
    CHECK( std::abs( p_ana - p_pde ) <= 0.1 );
    CHECK( std::abs( p_ana - p_mcl ) <= 6.0 * Trust( mcl ) + 5e-2 );
}

// --- American composite : early exercise on the USD value S*FX. The composite
// spot is a derived (non-diffusion) node, so its full path is only available once
// recording forces it to be scheduled at every exercise date; the LSM then prices
// above the European value and close to the PDE American oracle.
TEST_CASE( "American composite put exceeds European and matches the PDE oracle" )
{
    auto cfg = []( const std::string& method, const std::string& exercise )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: usd," << ( method == "ana" ? "" : method == "pde" ? " pde_configuration: pd,"
                                                                                                                                           : " mcl_configuration: m," )
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "m: !mcl_configuration {max_day_step: 7, min_day_step: -1, paths: 200000,"
          << " vol_year_step: 0.01, use_sobol: true}\n"
          << "pd: !pde_configuration {vanilla_precision: high}\n"
          << "eur: !currency {rate: r_eur}\n"
          << "r_eur: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
          << "usd: !currency {rate: r_usd}\n"
          << "r_usd: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
          << "eur/usd: !forex {base_currency: usd, underlying_currency: eur, spot: 1.5, volatility: fxvol}\n"
          << "fxvol: !bs_volatility {volatility: 15}\n"
          << "comp: !composite {equity: eq, composite_currency: usd}\n"
          << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd], matrix: [1, 0.5, 0.5, 1]}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: comp, premium_currency: usd, strike: 150,"
          << " maturity: 2000-12-31, type: put, exercise: " << exercise << "}\n";
        return o.str();
    };

    const double eu_mcl = Premium( Price( cfg( "mcl", "european" ) ) );
    auto am = Price( cfg( "mcl", "american" ) );
    const double am_mcl = Premium( am );
    const double am_pde = Premium( Price( cfg( "pde", "american" ) ) );
    CAPTURE( eu_mcl );
    CAPTURE( am_mcl );
    CAPTURE( am_pde );

    CHECK( am_mcl > eu_mcl );                             //!< early-exercise premium captured
    CHECK( am_mcl <= am_pde + 6.0 * Trust( am ) + 5e-2 ); //!< LSM is a (biased-low) lower bound
    CHECK( am_mcl >= am_pde * 0.95 );                     //!< within ~5% of the oracle
}

// --- regression : a non-Mono underlying (composite) priced with MCL Greeks goes
// through bump-and-revalue, which re-enters PriceBook (and ComputeCholeskyMatrix)
// several times. The cholesky working lists must be rebuilt each call, else they
// accumulate duplicate rows and the correlation is reported "not SDP". A few
// thousand paths suffice: this guards against the throw, not a numeric value.
TEST_CASE( "composite MCL Greeks do not corrupt the correlation Cholesky" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: usd, mcl_configuration: m,"
      << " correlation: cor, indicators: [premium, delta, vega, rho, theta], result: res}\n"
      << "m: !mcl_configuration {max_day_step: 30, min_day_step: -1, paths: 8000,"
      << " vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: r_eur}\n"
      << "r_eur: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "usd: !currency {rate: r_usd}\n"
      << "r_usd: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "eur/usd: !forex {base_currency: usd, underlying_currency: eur, spot: 1.5, volatility: fxvol}\n"
      << "fxvol: !bs_volatility {volatility: 15}\n"
      << "comp: !composite {equity: eq, composite_currency: usd}\n"
      << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd], matrix: [1, 0.5, 0.5, 1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: comp, premium_currency: usd, strike: 150,"
      << " maturity: 2000-12-31, type: call, exercise: european}\n";
    auto r = Price( o.str() ); //!< must not throw "cor is not SDP"
    CHECK( std::isfinite( Premium( r ) ) );
    CHECK( std::isfinite( Greek( r, "delta" ) ) );
    CHECK( Greek( r, "vega" ) > 0.0 ); //!< composite vega is positive
}

// --- SABR : with beta = 1 and a tiny vol-of-vol the ATM SABR implied vol is ~alpha,
// so an ATM call matches Black-Scholes at that vol (exercises the SABR surface).
TEST_CASE( "SABR ATM vanilla matches Black-Scholes at alpha" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !sabr_volatility {maturities: [1.0], alpha: [0.30], beta: [1.0],"
      << " rho: [0.0], nu: [0.01], calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    double p = Premium( Price( o.str() ) );
    CHECK( std::abs( p - BsCall( 100, 100, 0.05, 0.30, T1 ) ) <= 0.3 );
}

// --- SABR local-vol Monte-Carlo : the MCL engine samples the Dupire local-vol
// surface (built from the SABR implied surface) onto per-date log-spot grids and
// diffuses along them. By the Dupire repricing property a European vanilla under
// local vol must reproduce the implied-vol (ANA) price, both for a flat surface
// (constant local vol) and for a genuine smile (non-constant surface).
TEST_CASE( "SABR local-vol MCL reprices the implied surface (matches ANA)" )
{
    auto book = []( const std::string& method, const std::string& sabr )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur," << ConfigRef( method )
          << " correlation: cor, indicators: [premium], result: res}\n"
          << CfgBlock( 300000, 7, 6 )
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << "vol: !sabr_volatility {maturities: [1.0], alpha: [0.30], beta: [1.0], " << sabr
          << ", calendar: cal}\n"
          << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
          << " is_absolute_strike: true, maturity: 2000-12-31, type: call, exercise: european}\n";
        return o.str();
    };

    //! flat surface (nu -> 0): local vol is constant = alpha, so local-vol MC must
    //! reproduce the ANA (= BS) price within Monte-Carlo error
    {
        const std::string flat = "rho: [0.0], nu: [0.001]";
        double ana = Premium( Price( book( "ana", flat ) ) );
        auto mr = Price( book( "mcl", flat ) );
        double mcl = Premium( mr );
        CAPTURE( ana );
        CAPTURE( mcl );
        CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mr ) + 0.2 );
    }

    //! genuine smile: the Dupire local vol reprices the ATM implied vol, so the
    //! local-vol MC still matches ANA while now exercising a non-constant surface
    {
        const std::string smile = "rho: [-0.3], nu: [0.4]";
        double ana = Premium( Price( book( "ana", smile ) ) );
        auto mr = Price( book( "mcl", smile ) );
        double mcl = Premium( mr );
        CAPTURE( ana );
        CAPTURE( mcl );
        CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mr ) + 0.6 );
    }
}

// --- SABR local-vol PDE : the 1-D grid reads the Dupire local vol per node and per
// time step (like the MCL diffusion), so by the Dupire repricing property a European
// vanilla must reproduce the implied-vol (ANA) price AT EVERY STRIKE — not just ATM.
// Before the local-vol grid the PDE diffused the single ATM vol, so with a genuine
// smile (rho -0.3, nu 0.4) the OTM strikes mispriced vs ANA.
TEST_CASE( "SABR local-vol PDE reprices the implied surface across strikes" )
{
    auto book = []( const std::string& method, double strike )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur," << ConfigRef( method )
          << " correlation: cor, indicators: [premium], result: res}\n"
          << CfgBlock( 1, 7, 4 ) //!< medium PDE grid; draws unused (no MCL cell here)
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << "vol: !sabr_volatility {maturities: [1.0], alpha: [0.30], beta: [1.0],"
          << " rho: [-0.3], nu: [0.4], calendar: cal}\n"
          << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: eq, premium_currency: eur, strike: " << strike
          << ", is_absolute_strike: true, maturity: 2000-12-31, type: "
          << ( strike < 100 ? "put" : "call" ) << ", exercise: european}\n";
        return o.str();
    };

    //! OTM put, ATM call, OTM call: ANA prices each off the SABR implied vol AT THE
    //! STRIKE; the local-vol PDE must land on the same price (Dupire repricing).
    for ( double strike : { 80.0, 100.0, 120.0 } )
    {
        const double ana = Premium( Price( book( "ana", strike ) ) );
        const double pde = Premium( Price( book( "pde", strike ) ) );
        CAPTURE( strike );
        CAPTURE( ana );
        CAPTURE( pde );
        //! 1.5% relative + a small absolute floor for the cheap OTM wings (grid +
        //! Dupire finite-difference resolution)
        CHECK( std::abs( pde - ana ) <= 0.015 * ana + 0.05 );
    }
}

// --- basket local-vol : each component of a basket diffuses with its own Dupire
// local vol. A basket of flat SABR surfaces (constant local vol = alpha) must match
// the same basket priced with the equivalent bs_volatility, proving the local-vol
// components plug into the basket diffusion and reduce to constant vol.
TEST_CASE( "basket local-vol MCL: flat SABR components match the BS basket" )
{
    auto book = []( const std::string& vol_obj )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur, mcl_configuration: m,"
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "m: !mcl_configuration {max_day_step: 7, min_day_step: -1, paths: 200000,"
          << " vol_year_step: 0.01, use_sobol: true}\n"
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "eq1: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << "eq2: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << vol_obj
          << "cor: !correlation_matrix {underlyings: [eq1, eq2], matrix: [1, 0.3, 0.3, 1]}\n"
          << "bk: !basket {underlyings: [eq1, eq2], weights: [0.5, 0.5]}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: bk, premium_currency: eur, strike: 100,"
          << " is_absolute_strike: true, maturity: 2000-12-31, type: call, exercise: european}\n";
        return o.str();
    };
    const std::string bs = "vol: !bs_volatility {volatility: 30, calendar: cal}\n";
    const std::string sabr = "vol: !sabr_volatility {maturities: [1.0], alpha: [0.30],"
                             " beta: [1.0], rho: [0.0], nu: [0.001], calendar: cal}\n";
    double p_bs = Premium( Price( book( bs ) ) );
    auto sr = Price( book( sabr ) );
    double p_sabr = Premium( sr );
    CAPTURE( p_bs );
    CAPTURE( p_sabr );
    CHECK( std::abs( p_sabr - p_bs ) <= 6.0 * Trust( sr ) + 0.2 );
}

// --- composite local-vol : a EUR equity settled in USD, with a SABR surface. The
// inner spot diffuses on the full Dupire local vol; the quanto drift uses the ATM
// vol (as ANA/PDE do). By Dupire repricing the local-vol composite must match the
// ANA composite (which combines the strike implied vol with the FX vol).
TEST_CASE( "composite local-vol MCL reprices the implied surface (matches ANA)" )
{
    auto cfg = []( const std::string& method, const std::string& vol_obj )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: usd," << ( method == "ana" ? "" : " mcl_configuration: m," )
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "m: !mcl_configuration {max_day_step: 7, min_day_step: -1, paths: 300000,"
          << " vol_year_step: 0.01, use_sobol: true}\n"
          << "eur: !currency {rate: r_eur}\n"
          << "r_eur: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
          << "usd: !currency {rate: r_usd}\n"
          << "r_usd: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << vol_obj
          << "eur/usd: !forex {base_currency: usd, underlying_currency: eur, spot: 1.5, volatility: fxvol}\n"
          << "fxvol: !bs_volatility {volatility: 15}\n"
          << "comp: !composite {equity: eq, composite_currency: usd}\n"
          << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd], matrix: [1, 0.5, 0.5, 1]}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: comp, premium_currency: usd, strike: 150,"
          << " maturity: 2000-12-31, type: call, exercise: european}\n";
        return o.str();
    };
    const std::string bs = "vol: !bs_volatility {volatility: 30, calendar: cal}\n";
    const std::string flat = "vol: !sabr_volatility {maturities: [1.0], alpha: [0.30],"
                             " beta: [1.0], rho: [0.0], nu: [0.001], calendar: cal}\n";
    const std::string smile = "vol: !sabr_volatility {maturities: [1.0], alpha: [0.30],"
                              " beta: [1.0], rho: [-0.3], nu: [0.4], calendar: cal}\n";

    //! flat surface -> constant local vol -> matches the BS composite (machinery)
    double p_bs = Premium( Price( cfg( "mcl", bs ) ) );
    auto fr = Price( cfg( "mcl", flat ) );
    CAPTURE( p_bs );
    CAPTURE( Premium( fr ) );
    CHECK( std::abs( Premium( fr ) - p_bs ) <= 6.0 * Trust( fr ) + 0.3 );

    //! smile -> Dupire repricing -> matches the ANA composite (inner local vol
    //! reprices the equity smile; the quanto drift uses the ATM vol, as ANA/PDE do)
    double ana = Premium( Price( cfg( "ana", smile ) ) );
    auto mr = Price( cfg( "mcl", smile ) );
    CAPTURE( ana );
    CAPTURE( Premium( mr ) );
    CHECK( std::abs( Premium( mr ) - ana ) <= 6.0 * Trust( mr ) + 0.8 );
}

// --- Heston (MCL) : in the degenerate limit (vol-of-vol -> 0, v0 = theta) the
// stochastic-vol diffusion collapses to constant-vol GBM, so it matches BS.
TEST_CASE( "Heston MCL degenerate limit matches Black-Scholes" )
{
    std::ostringstream o;
    o << "root: p\n"
      << "p: !mcl_pricer {today: 2000-01-01, book: bk, currency: eur, mcl_configuration: m,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "m: !mcl_configuration {max_day_step: 7, min_day_step: -1, paths: 200000,"
      << " vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, 0, 0, 1]}\n"
      << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
      << "h: !heston_volatility {spot: 100, init_vol: 30, long_vol: 30, kappa: 5,"
      << " vol_of_vol: 0.0001, calendar: cal}\n"
      << "bk: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    auto mr = Price( o.str() );
    CHECK( std::abs( Premium( mr ) - BsCall( 100, 100, 0.08, 0.30, T1 ) ) <= 6.0 * Trust( mr ) + 0.05 );
}

// --- Heston (MCL) : a negative spot/vol correlation fattens the left wing, so an
// OTM put is richer than the flat-vol Black-Scholes put at the same ATM level.
TEST_CASE( "Heston MCL negative-rho skew enriches the OTM put" )
{
    std::ostringstream o;
    o << "root: p\n"
      << "p: !mcl_pricer {today: 2000-01-01, book: bk, currency: eur, mcl_configuration: m,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "m: !mcl_configuration {max_day_step: 5, min_day_step: -1, paths: 200000,"
      << " vol_year_step: 0.01, use_sobol: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [0, 0]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, -0.7, -0.7, 1]}\n"
      << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
      << "h: !heston_volatility {spot: 100, init_vol: 20, long_vol: 20, kappa: 2,"
      << " vol_of_vol: 0.5, calendar: cal}\n"
      << "bk: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 80,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: put, exercise: european}\n";
    double heston = Premium( Price( o.str() ) );
    CHECK( heston > BsPut( 100, 80, 0.0, 0.20, T1 ) + 0.2 ); //!< clearly richer than flat-vol
}

// --- Heston (ANA) : the characteristic-function price agrees with the MCL QE
// diffusion (the two engines, same model) within Monte-Carlo error.
TEST_CASE( "Heston ANA characteristic function agrees with MCL" )
{
    auto cfg = []( const std::string& method, const std::string& mcl )
    {
        std::ostringstream o;
        o << "root: p\n"
          << "p: !" << method << "_pricer {today: 2000-01-01, book: bk, currency: eur," << ( method == "ana" ? "" : " mcl_configuration: m," )
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "m: !mcl_configuration {max_day_step: 3, min_day_step: -1, paths: " << mcl
          << ", vol_year_step: 0.01, use_sobol: true}\n"
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [0, 0]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, -0.7, -0.7, 1]}\n"
          << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
          << "h: !heston_volatility {spot: 100, init_vol: 20, long_vol: 20, kappa: 2,"
          << " vol_of_vol: 0.5, calendar: cal}\n"
          << "bk: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 80,"
          << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: put, exercise: european}\n";
        return o.str();
    };
    double ana = Premium( Price( cfg( "ana", "1" ) ) );
    auto mr = Price( cfg( "mcl", "300000" ) );
    CHECK( std::abs( ana - Premium( mr ) ) <= 6.0 * Trust( mr ) + 0.02 );
}

// --- Heston (ANA) : degenerate vol-of-vol collapses to Black-Scholes (closed form).
TEST_CASE( "Heston ANA degenerate limit matches Black-Scholes" )
{
    std::ostringstream o;
    o << "root: p\n"
      << "p: !ana_pricer {today: 2000-01-01, book: bk, currency: eur,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, 0, 0, 1]}\n"
      << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
      << "h: !heston_volatility {spot: 100, init_vol: 30, long_vol: 30, kappa: 5,"
      << " vol_of_vol: 0.0001, calendar: cal}\n"
      << "bk: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    CHECK( Premium( Price( o.str() ) ) == doctest::Approx( BsCall( 100, 100, 0.08, 0.30, T1 ) ).epsilon( 0.01 ) );
}

// --- Heston (PDE) : the 2-D (S,v) ADI grid matches the ANA characteristic
// function across moneyness, and American >= European.
TEST_CASE( "Heston PDE 2-D ADI matches ANA and supports American" )
{
    auto cfg = []( const std::string& method, double strike, const std::string& type,
                   const std::string& exercise )
    {
        std::ostringstream o;
        o << "root: p\n"
          << "p: !" << method << "_pricer {today: 2000-01-01, book: bk, currency: eur," << ( method == "ana" ? "" : " pde_configuration: pc," )
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "pc: !pde_configuration {vanilla_precision: high}\n"
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, -0.7, -0.7, 1]}\n"
          << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
          << "h: !heston_volatility {spot: 100, init_vol: 25, long_vol: 25, kappa: 2,"
          << " vol_of_vol: 0.5, calendar: cal}\n"
          << "bk: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: eq, premium_currency: eur, strike: " << strike
          << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
          << ", exercise: " << exercise << "}\n";
        return o.str();
    };
    for ( double K : { 80.0, 100.0, 120.0 } )
    {
        CAPTURE( K );
        std::string type = ( K < 100 ) ? "put" : "call";
        double ana = Premium( Price( cfg( "ana", K, type, "european" ) ) );
        double pde = Premium( Price( cfg( "pde", K, type, "european" ) ) );
        CHECK( std::abs( ana - pde ) <= 0.05 ); //!< 2-D ADI vs closed form
    }
    //! American put carries an early-exercise premium over European
    double eu = Premium( Price( cfg( "pde", 80, "put", "european" ) ) );
    double am = Premium( Price( cfg( "pde", 80, "put", "american" ) ) );
    CHECK( am >= eu - 1e-6 );
}

// --- yield curve term-structure interpolation : a sloped curve [r_lo, r_hi] must
// price a mid-maturity (2005, ~halfway between the 2000/2010 pillars) call strictly
// between the two flat-pillar extremes — rho is positive for a call, so a higher
// effective rate gives a higher premium. Exercises Curve::GetCurveValue's
// linear-on-rate interpolation; a flat curve would price all three identically.
TEST_CASE( "yield curve: linear rate interpolation between pillars" )
{
    const std::string c = "o: !vanilla {underlying: eq, premium_currency: eur,"
                          " strike: 100, maturity: 2005-01-01, type: call, exercise: european}\n";
    auto with_rate = []( std::string yaml, const std::string& vals )
    {
        const std::string from = "values: [8, 8]";
        yaml.replace( yaml.find( from ), from.size(), "values: [" + vals + "]" );
        return yaml;
    };
    double lo = Premium( Price( with_rate( OneContract( "ana", c ), "4, 4" ) ) );
    double hi = Premium( Price( with_rate( OneContract( "ana", c ), "12, 12" ) ) );
    double mid = Premium( Price( with_rate( OneContract( "ana", c ), "4, 12" ) ) );
    CAPTURE( lo );
    CAPTURE( hi );
    CAPTURE( mid );
    CHECK( lo < mid );
    CHECK( mid < hi );
}

// --- SABR arbitrage-free wings : beyond ±2.5 ATM-sigma of log-moneyness the
// surface switches from Hagan's expansion (whose implied density turns negative
// in the far wings) to Benaim-Dodgson-Kainth power-law price tails matched in
// value and slope at the cutoff. The implied density must therefore stay
// positive across the whole scanned strike range, the vol must be continuous at
// the junction, and the Dupire local variance the MCL/PDE consume must no longer
// hit its 1e-8 emergency floor in the wings.
TEST_CASE( "SABR wings are butterfly-arbitrage-free (positive implied density)" )
{
    //! CEV smile (beta 0.5, 3y): raw Hagan develops 25 butterfly violations on
    //! this fixture's scan grid, ALL beyond 4.8 ATM-sigma — i.e. wings-only, which
    //! is exactly what the matched power-law tails (cutoff 2.5 sigma) repair. At
    //! extreme nu*sqrt(T) (e.g. nu ~ 1, T >= 5) Hagan also violates INSIDE the
    //! liquid band, where no wing treatment can help — that is the expansion
    //! itself failing (use shorter pillars or a genuine stochastic-vol model).
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [0, 0]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !sabr_volatility {maturities: [3.0], alpha: [3.0], beta: [0.5],"
      << " rho: [-0.3], nu: [0.5], calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {contracts: [c]}\n"
      << "c: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2003-01-01, type: call, exercise: european}\n";

    //! build + run once (the run cascades today into the market objects), then
    //! query the surface and the single directly
    std::streambuf* saved = std::cout.rdbuf();
    std::ostringstream sink;
    std::cout.rdbuf( sink.rdbuf() );
    ObjectManager manager( YamlConfig::from_string_t{}, o.str() );
    manager.ReadObjects( ROOT_NODE );
    manager.ExecuteTask();
    std::cout.rdbuf( saved );

    auto* vol = manager.collector().Get<Volatility>( "vol" );
    auto* eq = manager.collector().Get<Single>( "eq" );
    REQUIRE( vol != nullptr );
    REQUIRE( eq != nullptr );

    const date maturity( 2003, 1, 1 );
    const double F = 100;            //!< r = 0 -> forward = spot
    const double T = 1096.0 / 365.0; //!< 2000-01-01 -> 2003-01-01
    const double atm = vol->GetImplicitVol( 0, F, maturity );
    const double band = atm * std::sqrt( T );

    //! undiscounted Black call off the surface vol
    auto call = [&]( double K )
    {
        const double v = vol->GetImplicitVol( K, F, maturity );
        const double s = v * std::sqrt( T );
        const double d1 = std::log( F / K ) / s + 0.5 * s;
        return F * NormCdf( d1 ) - K * NormCdf( d1 - s );
    };

    //! (1) butterfly positivity: C(K) convex in K across ±5.5 ATM sigma —
    //! second differences on a geometric strike grid must be non-negative
    const int n = 220;
    const double k_lo = -5.5 * band, k_hi = 5.5 * band;
    const double dk = ( k_hi - k_lo ) / n;
    int violations = 0;
    double worst = 0;
    for ( int i = 1; i < n; i++ )
    {
        const double k = k_lo + i * dk;
        const double Km = F * std::exp( k - dk ), K0 = F * std::exp( k ), Kp = F * std::exp( k + dk );
        //! central second difference on the non-uniform (geometric) grid
        const double fly = ( call( Kp ) - call( K0 ) ) / ( Kp - K0 ) -
                           ( call( K0 ) - call( Km ) ) / ( K0 - Km );
        if ( fly < -1e-12 * F )
        {
            violations++;
            worst = std::min( worst, fly );
        }
    }
    CAPTURE( worst );
    CHECK( violations == 0 );

    //! (2) the vol is continuous across the wing junctions
    for ( double side : { -1.0, 1.0 } )
    {
        const double Kc = F * std::exp( side * 2.5 * band );
        const double v_in = vol->GetImplicitVol( Kc * ( 1 - side * 1e-4 ), F, maturity );
        const double v_out = vol->GetImplicitVol( Kc * ( 1 + side * 1e-4 ), F, maturity );
        CAPTURE( side );
        CHECK( std::abs( v_out - v_in ) <= 2e-3 );
    }

    //! (3) the Dupire local variance no longer hits its 1e-8 emergency floor in
    //! the wings (sqrt -> exactly 1e-4): scan ±4.5 sigma of log-spot
    int floor_hits = 0;
    for ( int i = 0; i <= 80; i++ )
    {
        const double k = -4.5 * band + i * ( 9.0 * band / 80 );
        const double lv = eq->GetLocalVolatility( F * std::exp( k ), maturity );
        if ( lv <= 1.0001e-4 )
        {
            floor_hits++;
        }
    }
    CHECK( floor_hits == 0 );
}

// --- Dupire local-vol upper cap: at extreme nu*sqrt(T) Hagan's expansion makes
// the Dupire denominator collapse towards 0+ in the wings, which used to let a
// spuriously HUGE local variance through (the old backstop only floored the
// negative side). GetLocalVolatility now caps the local vol at 5x the node's own
// implied vol, so the invariant below must hold at EVERY node of an extreme
// fixture — while a healthy near-ATM region stays untouched (far below the cap).
TEST_CASE( "Dupire local vol is capped in degenerate SABR wings" )
{
    //! nu = 1 on a 5y pillar: nu*sqrt(T) ~ 2.2, deep in the regime where the
    //! expansion misbehaves (documented in the README as needing a backstop)
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [0, 0]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !sabr_volatility {maturities: [5.0], alpha: [0.25], beta: [1.0],"
      << " rho: [-0.6], nu: [1.0], calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {contracts: [c]}\n"
      << "c: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2005-01-01, type: call, exercise: european}\n";

    std::streambuf* saved = std::cout.rdbuf();
    std::ostringstream sink;
    std::cout.rdbuf( sink.rdbuf() );
    ObjectManager manager( YamlConfig::from_string_t{}, o.str() );
    manager.ReadObjects( ROOT_NODE );
    manager.ExecuteTask();
    std::cout.rdbuf( saved );

    auto* vol = manager.collector().Get<Volatility>( "vol" );
    auto* eq = manager.collector().Get<Single>( "eq" );
    REQUIRE( vol != nullptr );
    REQUIRE( eq != nullptr );

    const date maturity( 2005, 1, 1 );
    const double F = 100; //!< r = 0 -> forward = spot
    const double T = YearFraction( date( 2000, 1, 1 ), maturity );
    const double atm = vol->GetImplicitVol( 0, F, maturity );
    const double band = atm * std::sqrt( T );

    //! (1) cap invariant: local vol <= 5x the node's implied vol everywhere on a
    //! wide ±6 sigma scan (the degenerate nodes clamp exactly to it, the healthy
    //! ones sit far below)
    for ( int i = 0; i <= 120; i++ )
    {
        const double k = -6.0 * band + i * ( 12.0 * band / 120 );
        const double K = F * std::exp( k );
        const double iv = vol->GetImplicitVol( K, F, maturity );
        const double lv = eq->GetLocalVolatility( K, maturity );
        CAPTURE( k );
        CHECK( lv <= 5.0 * iv * 1.0001 );
    }

    //! (2) the cap does not bite near the money: on a HEALTHY fixture (the 1y
    //! moderate smile below) the ATM local vol sits well under half the cap
    std::ostringstream h;
    h << "root: pricer\n"
      << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [0, 0]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !sabr_volatility {maturities: [1.0], alpha: [0.25], beta: [1.0],"
      << " rho: [-0.3], nu: [0.4], calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {contracts: [c]}\n"
      << "c: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, type: call, exercise: european}\n";
    std::cout.rdbuf( sink.rdbuf() );
    ObjectManager healthy( YamlConfig::from_string_t{}, h.str() );
    healthy.ReadObjects( ROOT_NODE );
    healthy.ExecuteTask();
    std::cout.rdbuf( saved );
    auto* heq = healthy.collector().Get<Single>( "eq" );
    auto* hvol = healthy.collector().Get<Volatility>( "vol" );
    REQUIRE( heq != nullptr );
    const date mat1( 2000, 12, 31 );
    const double iv1 = hvol->GetImplicitVol( 0, F, mat1 );
    const double lv1 = heq->GetLocalVolatility( F, mat1 );
    CHECK( lv1 < 2.5 * iv1 );
}
