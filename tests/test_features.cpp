#include "helpers.hpp"
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
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( method, draws, step, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << extra_objects
      << "book: !book {options: [o]}\n"
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
    const std::string c = "o: !variance_swap {underlying: eq, premium_currency: eur,"
                          " maturity: 2000-12-31, volatility_strike: 20, notional: 10000}\n";
    double ana = Premium( Price( OneContract( "ana", c ) ) );
    auto mr = Price( OneContract( "mcl", c ) );
    double mcl = Premium( mr );

    CHECK( ana > 0.0 );                              //!< 30% realized vs 20% strike -> positive
    CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mr ) + 2.0 ); //!< engines agree (MC error)
}

// --- Sobol QMC : a use_sobol MCL run prices a vanilla, is deterministic across
// runs, and lands within MC error of Black-Scholes (exercises path_generator /
// sobol_generator).
TEST_CASE( "Sobol MCL: deterministic and converges to Black-Scholes" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << "cfg: !pricer_configuration {method: mcl, mcl_configuration: m, log_path: \"/tmp/\"}\n"
      << "m: !mcl_configuration {max_time_step: 30, min_time_step: -1, paths: 50000,"
      << " vol_time_step: 0.01, use_sobol: true, use_milstein: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [6, 6]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    double a = Premium( Price( o.str() ) );
    double b = Premium( Price( o.str() ) );
    CHECK( a == doctest::Approx( b ).epsilon( 1e-12 ) );        //!< deterministic
    CHECK( std::abs( a - BsCall( 100, 100, 0.06, 0.30, T1 ) ) <= 0.2 ); //!< QMC accuracy
}

// --- !sequence : runs each sub-task in order, each writing its own result block.
TEST_CASE( "sequence runs every sub-task" )
{
    std::ostringstream o;
    o << "root: seq\n"
      << "seq: !sequence {tasks: [p_pde, p_ana], result: seq_result}\n"
      << "p_pde: !pricer {today: 2000-01-01, book: book, currency: eur, configuration: cpde,"
      << " correlation: cor, indicators: [premium], result: pde_res}\n"
      << "p_ana: !pricer {today: 2000-01-01, book: book, currency: eur, configuration: cana,"
      << " correlation: cor, indicators: [premium], result: ana_res}\n"
      << "cpde: !pricer_configuration {method: pde, pde_configuration: pcfg, log_path: \"/tmp/\"}\n"
      << "cana: !pricer_configuration {method: ana, log_path: \"/tmp/\"}\n"
      << "pcfg: !pde_configuration {vanilla_precision: high}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {options: [o]}\n"
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
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur, configuration: cfg,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "cfg: !pricer_configuration {method: mcl, mcl_configuration: m, log_path: \"/tmp/\"}\n"
      << "m: !mcl_configuration {max_time_step: 30, min_time_step: -1, paths: 60000,"
      << " vol_time_step: 0.01, use_sobol: true, use_milstein: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq1: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "eq2: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq1, eq2], matrix: [1, 0.99, 0.99, 1]}\n"
      << "bk: !basket {underlyings: [eq1, eq2], weights: [0.5, 0.5]}\n"
      << "book: !book {options: [o]}\n"
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
          << "pricer: !pricer {today: 2000-01-01, book: book, currency: usd, configuration: cfg,"
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "cfg: !pricer_configuration {method: " << method
          << ", mcl_configuration: m, pde_configuration: pd, log_path: \"/tmp/\"}\n"
          << "m: !mcl_configuration {max_time_step: 7, min_time_step: -1, paths: 200000,"
          << " vol_time_step: 0.01, use_sobol: true, use_milstein: true}\n"
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
          << "book: !book {options: [o]}\n"
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
          << "pricer: !pricer {today: 2000-01-01, book: book, currency: usd, configuration: cfg,"
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "cfg: !pricer_configuration {method: " << method
          << ", mcl_configuration: m, pde_configuration: pd, log_path: \"/tmp/\"}\n"
          << "m: !mcl_configuration {max_time_step: 7, min_time_step: -1, paths: 200000,"
          << " vol_time_step: 0.01, use_sobol: true, use_milstein: true}\n"
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
          << "book: !book {options: [o]}\n"
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

    CHECK( am_mcl > eu_mcl );                            //!< early-exercise premium captured
    CHECK( am_mcl <= am_pde + 6.0 * Trust( am ) + 5e-2 ); //!< LSM is a (biased-low) lower bound
    CHECK( am_mcl >= am_pde * 0.95 );                   //!< within ~5% of the oracle
}

// --- regression : a non-Mono underlying (composite) priced with MCL Greeks goes
// through bump-and-revalue, which re-enters PriceBook_ (and ComputeCholeskyMatrix)
// several times. The cholesky working lists must be rebuilt each call, else they
// accumulate duplicate rows and the correlation is reported "not SDP". A few
// thousand paths suffice: this guards against the throw, not a numeric value.
TEST_CASE( "composite MCL Greeks do not corrupt the correlation Cholesky" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: usd, configuration: cfg,"
      << " correlation: cor, indicators: [premium, delta, vega, rho, theta], result: res}\n"
      << "cfg: !pricer_configuration {method: mcl, mcl_configuration: m, log_path: \"/tmp/\"}\n"
      << "m: !mcl_configuration {max_time_step: 30, min_time_step: -1, paths: 8000,"
      << " vol_time_step: 0.01, use_sobol: true, use_milstein: true}\n"
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
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: comp, premium_currency: usd, strike: 150,"
      << " maturity: 2000-12-31, type: call, exercise: european}\n";
    auto r = Price( o.str() );                 //!< must not throw "cor is not SDP"
    CHECK( std::isfinite( Premium( r ) ) );
    CHECK( std::isfinite( Greek( r, "delta" ) ) );
    CHECK( Greek( r, "vega" ) > 0.0 );         //!< composite vega is positive
}

// --- SABR : with beta = 1 and a tiny vol-of-vol the ATM SABR implied vol is ~alpha,
// so an ATM call matches Black-Scholes at that vol (exercises the SABR surface).
TEST_CASE( "SABR ATM vanilla matches Black-Scholes at alpha" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur, configuration: cfg,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "cfg: !pricer_configuration {method: ana, log_path: \"/tmp/\"}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !sabr_volatility {spot: 100, maturities: [1.0], alpha: [0.30], beta: [1.0],"
      << " rho: [0.0], nu: [0.01], calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    double p = Premium( Price( o.str() ) );
    CHECK( std::abs( p - BsCall( 100, 100, 0.05, 0.30, T1 ) ) <= 0.3 );
}

// --- Heston (MCL) : in the degenerate limit (vol-of-vol -> 0, v0 = theta) the
// stochastic-vol diffusion collapses to constant-vol GBM, so it matches BS.
TEST_CASE( "Heston MCL degenerate limit matches Black-Scholes" )
{
    std::ostringstream o;
    o << "root: p\n"
      << "p: !pricer {today: 2000-01-01, book: bk, currency: eur, configuration: c,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "c: !pricer_configuration {method: mcl, mcl_configuration: m, log_path: \"/tmp/\"}\n"
      << "m: !mcl_configuration {max_time_step: 7, min_time_step: -1, paths: 200000,"
      << " vol_time_step: 0.01, use_sobol: true, use_milstein: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, 0, 0, 1]}\n"
      << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
      << "h: !heston_volatility {spot: 100, init_vol: 30, long_vol: 30, kappa: 5,"
      << " vol_of_vol: 0.0001, calendar: cal}\n"
      << "bk: !book {options: [o]}\n"
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
      << "p: !pricer {today: 2000-01-01, book: bk, currency: eur, configuration: c,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "c: !pricer_configuration {method: mcl, mcl_configuration: m, log_path: \"/tmp/\"}\n"
      << "m: !mcl_configuration {max_time_step: 5, min_time_step: -1, paths: 200000,"
      << " vol_time_step: 0.01, use_sobol: true, use_milstein: true}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [0, 0]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, -0.7, -0.7, 1]}\n"
      << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
      << "h: !heston_volatility {spot: 100, init_vol: 20, long_vol: 20, kappa: 2,"
      << " vol_of_vol: 0.5, calendar: cal}\n"
      << "bk: !book {options: [o]}\n"
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
          << "p: !pricer {today: 2000-01-01, book: bk, currency: eur, configuration: c,"
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "c: !pricer_configuration {method: " << method << ", mcl_configuration: m, log_path: \"/tmp/\"}\n"
          << "m: !mcl_configuration {max_time_step: 3, min_time_step: -1, paths: " << mcl
          << ", vol_time_step: 0.01, use_sobol: true, use_milstein: true}\n"
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [0, 0]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, -0.7, -0.7, 1]}\n"
          << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
          << "h: !heston_volatility {spot: 100, init_vol: 20, long_vol: 20, kappa: 2,"
          << " vol_of_vol: 0.5, calendar: cal}\n"
          << "bk: !book {options: [o]}\n"
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
      << "p: !pricer {today: 2000-01-01, book: bk, currency: eur, configuration: c,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "c: !pricer_configuration {method: ana, log_path: \"/tmp/\"}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, 0, 0, 1]}\n"
      << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
      << "h: !heston_volatility {spot: 100, init_vol: 30, long_vol: 30, kappa: 5,"
      << " vol_of_vol: 0.0001, calendar: cal}\n"
      << "bk: !book {options: [o]}\n"
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
          << "p: !pricer {today: 2000-01-01, book: bk, currency: eur, configuration: c,"
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "c: !pricer_configuration {method: " << method << ", pde_configuration: pc, log_path: \"/tmp/\"}\n"
          << "pc: !pde_configuration {vanilla_precision: high}\n"
          << "eur: !currency {rate: rate}\n"
          << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "cor: !correlation_matrix {underlyings: [eq, eq_var], matrix: [1, -0.7, -0.7, 1]}\n"
          << "eq: !equity {spot: 100, volatility: h, currency: eur}\n"
          << "h: !heston_volatility {spot: 100, init_vol: 25, long_vol: 25, kappa: 2,"
          << " vol_of_vol: 0.5, calendar: cal}\n"
          << "bk: !book {options: [o]}\n"
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

// --- historical volatility : EWMA of log returns of a price series -> a positive
// annualised vol (exercises historical_volatility_computation).
TEST_CASE( "historical volatility computation" )
{
    std::ostringstream o;
    o << "root: hv\n"
      << "hv: !historical_volatility_computation {half_life: 30, time_step: 1,"
      << " values: [100, 101, 99, 102, 98, 103, 101, 100, 99, 101, 102, 100],"
      << " result: hv_res}\n";
    YAML::Node r = Price( o.str(), "hv" );
    double vol = r["hv_res"]["historical_volatility"].as<double>();
    CHECK( vol > 0.0 );
    CHECK( std::isfinite( vol ) );
}

// --- historical correlation : EWMA correlation of two fixing series, written
// back into the correlation object (exercises historical_correlation_computation
// and simple_fixing_data). The result is a valid correlation matrix (unit diagonal).
TEST_CASE( "historical correlation computation" )
{
    const std::string dates = "[2000-01-01, 2000-01-02, 2000-01-03, 2000-01-04,"
                              " 2000-01-05, 2000-01-06, 2000-01-07, 2000-01-08]";
    std::ostringstream o;
    o << "root: hc\n"
      << "hc: !historical_correlation_computation {half_life: 30, time_step: 1, range_size: 5,"
      << " correlation: cor, historical_spots_fixings: [f1, f2], result: hc_res}\n"
      << "cor: !correlation_matrix {underlyings: [a, b], matrix: [1, 0, 0, 1]}\n"
      << "f1: !simple_fixing_data {dates: " << dates
      << ", values: [100, 101, 102, 101, 103, 104, 103, 105], underlying: a}\n"
      << "f2: !simple_fixing_data {dates: " << dates
      << ", values: [50, 50.4, 50.8, 50.5, 51.2, 51.6, 51.3, 52], underlying: b}\n";
    YAML::Node r = Price( o.str(), "hc" );
    //! the computed correlation matrix is written back onto 'cor' (unit diagonal)
    double m00 = r["cor"]["matrix"][0].as<double>();
    CHECK( m00 == doctest::Approx( 1.0 ).epsilon( 1e-6 ) );
}
