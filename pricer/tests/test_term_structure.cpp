#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Term-structured discounting / drift. On a STEEP yield curve the Monte-Carlo spot
// must grow at the forward carry implied by the whole curve, not at a single
// front-pillar rate. Before the fix the MCL drift used the flat front-pillar rate,
// so a long-dated forward (and hence the option) was mispriced versus the analytic
// and PDE engines, which both build the forward from the zero rate to maturity.
// With the term-structured rate node the three engines agree again.

namespace
{
//! one 3y European call on a flat-vol equity, priced under a steep rate curve
//! (2% at the front, 10% at the long end). method selects the engine.
std::string TsCfg( const std::string& method )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << "cfg_mcl: !mcl_configuration {max_day_step: 7, min_day_step: -1,"
      << " paths: 400000, vol_year_step: 0.01, use_sobol: true}\n"
      << "cfg_pde: !pde_configuration {vanilla_precision: high}\n"
      << "eur: !currency {rate: rate}\n"
      //! steep curve: 2% at t=0 rising to 10% at the 4y pillar
      << "rate: !yield_curve {dates: [2000-01-01, 2004-01-01], values: [2, 10]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 25, calendar: cal}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2003-01-01, nominal: 1,"
      << " type: call, exercise: european}\n";
    return o.str();
}
} // namespace

TEST_CASE( "MCL reproduces the analytic price under a steep rate curve" )
{
    const double ana = Premium( Price( TsCfg( "ana" ) ) );
    auto mcl = Price( TsCfg( "mcl" ) );
    CAPTURE( ana );
    CAPTURE( Premium( mcl ) );
    //! the front-pillar-drift bug underpriced this call by several currency units
    //! (~6 on a ~17 premium); the term-structured drift brings MCL within MC error
    CHECK( Premium( mcl ) == doctest::Approx( ana ).epsilon( 0.01 ) );
}

TEST_CASE( "PDE reproduces the analytic price under a steep rate curve" )
{
    const double ana = Premium( Price( TsCfg( "ana" ) ) );
    const double pde = Premium( Price( TsCfg( "pde" ) ) );
    CAPTURE( ana );
    CAPTURE( pde );
    CHECK( pde == doctest::Approx( ana ).epsilon( 0.01 ) );
}

// --- American exercise on a steep curve -------------------------------------
//
// The LSM pass must discount each interior exercise cashflow at the zero rate of
// ITS OWN date, exactly like the term-structured diffusion drift and the European
// ContractNode discounting. Before the fix it used the single zero rate read at
// the contract maturity: on this 2% -> 10% curve that over-discounted the early
// exercise payoffs and understated the backward-rolled continuation values — the
// American put lost a third of its early-exercise premium (LSM 10.66 vs the
// binomial-oracle truth 11.15; the fixed LSM reads 11.03, a proper lower bound).
//
// The PDE American is NOT used as the oracle here: it prices the American in an
// effective flat-r_T world (drift AND discount at the zero rate to maturity —
// correct terminal forward/df, so its EUROPEAN prices are exact, but the interim
// exercise dynamics are those of the flat 8% world). On this curve that reads
// 9.64 — matching a flat-8% binomial (9.64), far below the true 11.15. A
// documented engine limitation; the last test below pins it so a future PDE
// term-structure fix updates it consciously.

namespace
{
//! Independent oracle: CRR binomial American put under the SAME term-structured
//! curve (cc zero rate linear between the 2000-01-01 / 2004-01-01 pillars,
//! ACT/365), stepping at the per-interval FORWARD rate f_i = (r2·t2 - r1·t1)/dt
//! for both the risk-neutral drift and the per-step discount — i.e. the true
//! curve dynamics the MCL diffusion follows. FlatRate <= 0 selects the curve;
//! a positive FlatRate prices the flat-rate world instead (the PDE's effective
//! model). 2000 steps sit well within 0.1% of converged (6000 steps: 11.1538).
double TsAmericanPutBinomial( double S0, double K, double sig, double FlatRate = 0 )
{
    const double T = 1096.0 / 365.0;  //!< 2000-01-01 -> 2003-01-01
    const double Tp = 1461.0 / 365.0; //!< 2000-01-01 -> 2004-01-01 (last pillar)
    auto zero = [&]( double t )
    { return FlatRate > 0 ? FlatRate : 0.02 + ( 0.10 - 0.02 ) * std::min( t, Tp ) / Tp; };

    const int N = 2000;
    const double dt = T / N;
    const double u = std::exp( sig * std::sqrt( dt ) );
    const double d = 1.0 / u;

    std::vector<double> V( N + 1 );
    for ( int j = 0; j <= N; j++ )
    {
        V[j] = std::max( K - S0 * std::pow( u, j ) * std::pow( d, N - j ), 0.0 );
    }
    for ( int i = N - 1; i >= 0; i-- )
    {
        const double t1 = i * dt, t2 = ( i + 1 ) * dt;
        const double f = ( zero( t2 ) * t2 - zero( t1 ) * t1 ) / dt; //!< forward rate over the step
        const double g = std::exp( f * dt );
        const double p = ( g - d ) / ( u - d );
        for ( int j = 0; j <= i; j++ )
        {
            const double cont = ( p * V[j + 1] + ( 1 - p ) * V[j] ) / g;
            const double S = S0 * std::pow( u, j ) * std::pow( d, i - j );
            V[j] = std::max( cont, K - S );
        }
    }
    return V[0];
}

//! one 3y vanilla on a flat-vol equity under the steep 2% -> 10% curve above.
//! 30-day MCL steps keep the LSM recording matrix small at 200k paths.
std::string TsAmerCfg( const std::string& method, const std::string& type,
                       const std::string& exercise )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << "cfg_mcl: !mcl_configuration {max_day_step: 30, min_day_step: -1,"
      << " paths: 200000, vol_year_step: 0.01, use_sobol: true}\n"
      << "cfg_pde: !pde_configuration {vanilla_precision: high}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2004-01-01], values: [2, 10]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 25, calendar: cal}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2003-01-01, nominal: 1,"
      << " type: " << type << ", exercise: " << exercise << "}\n";
    return o.str();
}
} // namespace

TEST_CASE( "American call equals the European call under a steep rate curve" )
{
    //! exact oracle: no dividends + positive rates => never exercise early, so the
    //! American call IS the European call (ANA prices it off the full curve). This
    //! guards the LSM maturity-leg discounting (exact df to maturity).
    const double euro = Premium( Price( TsAmerCfg( "ana", "call", "european" ) ) );
    auto amer = Price( TsAmerCfg( "mcl", "call", "american" ) );
    CAPTURE( euro );
    CAPTURE( Premium( amer ) );
    CHECK( std::abs( Premium( amer ) - euro ) <= 6.0 * Trust( amer ) + 5e-2 );
}

TEST_CASE( "American put LSM matches the term-structure binomial oracle" )
{
    const double oracle = TsAmericanPutBinomial( 100, 100, 0.25 ); //!< ~11.15
    const double euro = Premium( Price( TsAmerCfg( "ana", "put", "european" ) ) );
    auto amer = Price( TsAmerCfg( "mcl", "put", "american" ) );
    const double lsm = Premium( amer );
    CAPTURE( oracle );
    CAPTURE( euro );
    CAPTURE( lsm );
    CHECK( lsm > euro ); //!< positive early-exercise premium survives the steep curve
    //! LSM is a (slightly low) lower bound of the oracle: the fixed discounting
    //! reads ~11.03 vs 11.15 (-1%); the old flat-maturity-rate discounting read
    //! ~10.66 (-4.4%) and fails this floor.
    CHECK( lsm >= oracle * 0.965 );
    CHECK( lsm <= oracle + 6.0 * Trust( amer ) + 5e-2 ); //!< and never materially above it
}

TEST_CASE( "PDE American matches the term-structure binomial oracle on a steep curve" )
{
    //! The grid steps at the per-interval FORWARD carry/discount rates read off the
    //! full curves (InitGrid `_fwd_carry`/`_fwd_disc`), so the American interim
    //! exercise sees the true curve dynamics. Before that fix the whole solve used
    //! the zero rate at maturity — an effective flat-8% world reading ~9.64 here,
    //! 13% below the truth (European prices were exact either way: same terminal
    //! forward and df). Now PDE, MCL (LSM) and the oracle agree.
    const double oracle = TsAmericanPutBinomial( 100, 100, 0.25 ); //!< ~11.15
    const double pde = Premium( Price( TsAmerCfg( "pde", "put", "american" ) ) );
    CAPTURE( oracle );
    CAPTURE( pde );
    CHECK( pde == doctest::Approx( oracle ).epsilon( 0.005 ) );
}
