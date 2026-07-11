#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Term-structured correlation: a !correlation_matrix with a `maturities` pillar
// list carries one matrix per pillar, entries piecewise-linear in time (flat
// beyond the pillars). The engines consume integrated views: the running average
// rho_bar(T) for the analytic quanto / composite / basket formulas, and per-step
// Cholesky factors of the step-average matrices for the Monte-Carlo increments.

namespace
{

//! test-side oracle: the running average over [0, T] of the piecewise-linear
//! entry that is c1 up to m1, lerped to c2 at m2 and flat after (independent
//! arithmetic mirroring the engine's closed form)
double RhoBar( double c1, double c2, double m1, double m2, double T )
{
    auto instant = [&]( double t )
    {
        if ( t <= m1 )
            return c1;
        if ( t >= m2 )
            return c2;
        return c1 + ( t - m1 ) / ( m2 - m1 ) * ( c2 - c1 );
    };
    //! primitive of the three segments up to T
    double F = 0, t = 0;
    for ( double knot : { m1, m2 } )
    {
        double up = std::min( T, knot );
        if ( up > t )
        {
            F += ( instant( t ) + instant( up ) ) / 2 * ( up - t );
            t = up;
        }
    }
    if ( T > t )
    {
        F += c2 * ( T - t );
    }
    return F / T;
}

//! quanto vanilla under a 2-pillar (asset, FX) correlation term structure; a
//! single pillar list entry count of 1 is not term-structured, so both matrices
//! are always given (concatenated row-major)
std::string TermQuantoCfg( double c1, double c2, double m1, double m2,
                           const std::string& method,
                           int draws = 1, int pde_precision = 5 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: usd,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( draws, 30, pde_precision )
      << "eur: !currency {rate: eur_rate}\n"
      << "eur_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "usd: !currency {rate: usd_rate}\n"
      << "usd_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "eur/usd: !forex {base_currency: usd, underlying_currency: eur,"
      << " spot: 1.5, volatility: fxvol}\n"
      << "fxvol: !bs_volatility {volatility: 15}\n"
      << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd],"
      << " maturities: [" << m1 << ", " << m2 << "],"
      << " matrix: [1, " << c1 << ", " << c1 << ", 1,"
      << "          1, " << c2 << ", " << c2 << ", 1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: usd, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call"
      << ", exercise: european}\n";
    return o.str();
}

//! 50/50 two-asset basket call; `correl` is either a constant entry or a
//! 2-pillar term structure (both matrices in symmetric/lower-triangle form, so
//! the pillar chunking of that field is exercised too)
std::string BasketCfg( const std::string& correl, const std::string& method, int draws )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( draws, 30, 5 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq1: !equity {spot: 100, volatility: vol1, currency: eur}\n"
      << "eq2: !equity {spot: 100, volatility: vol2, currency: eur}\n"
      << "vol1: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "vol2: !bs_volatility {volatility: 20, calendar: cal}\n"
      << "cor: !correlation_matrix {underlyings: [eq1, eq2], " << correl << "}\n"
      << "bk: !basket {underlyings: [eq1, eq2], weights: [0.5, 0.5]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: bk, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n";
    return o.str();
}

std::string TermSymmetric( double c1, double c2, double m1, double m2 )
{
    std::ostringstream o;
    o << "maturities: [" << m1 << ", " << m2 << "],"
      << " symmetric_matrix: [1, " << c1 << ", 1,   1, " << c2 << ", 1]";
    return o.str();
}

} // namespace

// The analytic quanto drift integrates rho over [0, T]: pricing with the pillar
// matrices must equal pricing with a CONSTANT matrix at the running average
// rho_bar(T) — exactly, since the engine's piecewise-linear average is closed
// form. Pillars at 0.25 and 0.75 with maturity 1 exercise all three segments
// (flat before, linear between, flat after).
TEST_CASE( "term correlation: ANA quanto equals the constant matrix at rho_bar(T)" )
{
    const double m1 = 0.25, m2 = 0.75;
    for ( auto [c1, c2] : { std::pair{ 0.8, -0.4 }, std::pair{ -0.5, 0.5 },
                            std::pair{ 0.3, 0.3 } } )
    {
        CAPTURE( c1 );
        CAPTURE( c2 );
        double rho_bar = RhoBar( c1, c2, m1, m2, T1 );

        double term = Premium( Price( TermQuantoCfg( c1, c2, m1, m2, "ana" ) ) );
        double flat = Premium( Price( QuantoVanillaCfg( 100, 100, 30, 8, 5, 15,
                                                        rho_bar, "call", "ana" ) ) );
        CHECK( term == doctest::Approx( flat ).epsilon( 1e-10 ) );
    }
}

// Cross-engine agreement on the term-correlated quanto: the closed-form
// reference is the constant-rho quanto Black-Scholes at rho_bar(T). The MCL path
// correlates each step with the step-average matrix (running average in the
// quanto adjustment node), the PDE telescopes the per-step spread — all three
// must reproduce the same integral.
TEST_CASE( "term correlation: ANA, PDE and MCL agree on a quanto vanilla" )
{
    const double m1 = 0.25, m2 = 0.75, c1 = 0.8, c2 = -0.4;
    const double rho_bar = RhoBar( c1, c2, m1, m2, T1 );

    double ref = QuantoBsCall( 100, 100, 0.08, 0.05, 0.30, 0.15, rho_bar, T1 );

    double ana = Premium( Price( TermQuantoCfg( c1, c2, m1, m2, "ana" ) ) );
    double pde = Premium( Price( TermQuantoCfg( c1, c2, m1, m2, "pde" ) ) );
    auto mr = Price( TermQuantoCfg( c1, c2, m1, m2, "mcl", 80000 ) );
    double mcl = Premium( mr );

    CHECK( std::abs( ana - ref ) <= 1e-2 );                     //!< closed form
    CHECK( std::abs( pde - ref ) <= 0.05 );                     //!< grid discretisation
    CHECK( std::abs( mcl - ref ) <= 6.0 * Trust( mr ) + 1e-2 ); //!< MC error
}

// Two-asset basket under a strongly decaying correlation (0.9 -> -0.3): the
// basket's terminal joint law only depends on the INTEGRATED covariance, so the
// term-structured book must price like the equivalent constant matrix at
// rho_bar(T) — exactly for the analytic moment matcher, within the MC error for
// the per-step-Cholesky Monte-Carlo. The term structure is fed in the
// symmetric/lower-triangle form to exercise that pillar chunking too.
TEST_CASE( "term correlation: 2-asset basket matches the equivalent constant matrix" )
{
    const double m1 = 0.25, m2 = 0.75, c1 = 0.9, c2 = -0.3;
    const double rho_bar = RhoBar( c1, c2, m1, m2, T1 );

    std::ostringstream flat;
    flat << "matrix: [1, " << rho_bar << ", " << rho_bar << ", 1]";

    double ana_term = Premium( Price( BasketCfg( TermSymmetric( c1, c2, m1, m2 ), "ana", 1 ) ) );
    double ana_flat = Premium( Price( BasketCfg( flat.str(), "ana", 1 ) ) );
    CHECK( ana_term == doctest::Approx( ana_flat ).epsilon( 1e-10 ) );

    auto mr_term = Price( BasketCfg( TermSymmetric( c1, c2, m1, m2 ), "mcl", 100000 ) );
    auto mr_flat = Price( BasketCfg( flat.str(), "mcl", 100000 ) );
    double mcl_term = Premium( mr_term );
    double mcl_flat = Premium( mr_flat );
    CHECK( std::abs( mcl_term - mcl_flat ) <=
           6.0 * ( Trust( mr_term ) + Trust( mr_flat ) ) + 1e-2 );

    //! and the MC agrees with its own analytic moment matching
    CHECK( std::abs( mcl_term - ana_term ) <= 6.0 * Trust( mr_term ) + 0.05 );
}

// Two pivot-anchored FX pairs plus an equity in one MCL book: the Cholesky
// extraction now fills the fx/fx block (>= 2 pairs), pinning the fixed
// (ud_size+i, ud_size+j) indexing — the old (i, j+fx_size) write was out of
// bounds here — and the term averaging of the fx/eq and fx/fx cells. A wrong or
// corrupted factor would break the row norms, hence the MARGINAL vols, so each
// option must still match its own closed form (Black on the covered-parity FX
// forward; ANA rejects forex vanillas, so the references are test-side).
TEST_CASE( "term correlation: two-FX-pair book preserves every marginal (fx/fx block)" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !mcl_pricer {today: 2000-01-01, book: book, currency: eur,"
      << ConfigRef( "mcl" ) << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( 100000, 30, 5 )
      << "eur: !currency {rate: eur_rate}\n"
      << "eur_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
      << "usd: !currency {rate: usd_rate}\n"
      << "usd_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "jpy: !currency {rate: jpy_rate}\n"
      << "jpy_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [1, 1]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "eur/usd: !forex {base_currency: usd, underlying_currency: eur,"
      << " spot: 1.5, volatility: fxvol1}\n"
      << "fxvol1: !bs_volatility {volatility: 15}\n"
      << "eur/jpy: !forex {base_currency: jpy, underlying_currency: eur,"
      << " spot: 160, volatility: fxvol2}\n"
      << "fxvol2: !bs_volatility {volatility: 12}\n"
      //! order [eq | eur/usd, eur/jpy]; both pillars PSD
      << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd, eur/jpy],"
      << " maturities: [0.25, 0.75],"
      << " matrix: [1.0, 0.5, 0.3,   0.5, 1.0, 0.6,   0.3, 0.6, 1.0,"
      << "          1.0, -0.2, 0.1,  -0.2, 1.0, 0.2,  0.1, 0.2, 1.0]}\n"
      << "book: !book {contracts: [o_eq, o_fx1, o_fx2]}\n"
      << "o_eq: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call,"
      << " exercise: european}\n"
      << "o_fx1: !vanilla {underlying: eur/usd, premium_currency: usd, strike: 1.5,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call,"
      << " exercise: european}\n"
      << "o_fx2: !vanilla {underlying: eur/jpy, premium_currency: jpy, strike: 160,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call,"
      << " exercise: european}\n";

    //! Black on a forward: df * (F N(d1) - K N(d2))
    auto black = []( double F, double K, double df, double sig, double T )
    {
        const double sd = sig * std::sqrt( T );
        const double d1 = std::log( F / K ) / sd + sd / 2;
        return df * ( F * NormCdf( d1 ) - K * NormCdf( d1 - sd ) );
    };
    //! FX forward by covered parity: F = S * exp((r_dom - r_for) T)
    const double ref_eq = BsCall( 100, 100, 0.08, 0.30, T1 );
    const double ref_fx1 = black( 1.5 * std::exp( ( 0.05 - 0.08 ) * T1 ), 1.5,
                                  std::exp( -0.05 * T1 ), 0.15, T1 );
    const double ref_fx2 = black( 160 * std::exp( ( 0.01 - 0.08 ) * T1 ), 160.0,
                                  std::exp( -0.01 * T1 ), 0.12, T1 );

    auto mcl = Price( o.str() );
    for ( auto [c, ref] : { std::pair{ std::string( "o_eq" ), ref_eq },
                            std::pair{ std::string( "o_fx1" ), ref_fx1 },
                            std::pair{ std::string( "o_fx2" ), ref_fx2 } } )
    {
        CAPTURE( c );
        double m = Premium( mcl, c );
        double tr = Trust( mcl, c );
        CHECK( std::abs( m - ref ) <= 6.0 * tr + 1e-3 );
    }
}

// Invalid term structures must fail loudly at load.
TEST_CASE( "term correlation: invalid pillar inputs are rejected" )
{
    auto quanto_with_cor = []( const std::string& cor )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: usd,"
          << " correlation: cor, indicators: [premium], result: res}\n"
          << "eur: !currency {rate: eur_rate}\n"
          << "eur_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [8, 8]}\n"
          << "usd: !currency {rate: usd_rate}\n"
          << "usd_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
          << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
          << "eur/usd: !forex {base_currency: usd, underlying_currency: eur,"
          << " spot: 1.5, volatility: fxvol}\n"
          << "fxvol: !bs_volatility {volatility: 15}\n"
          << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd], " << cor << "}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: eq, premium_currency: usd, strike: 100,"
          << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call"
          << ", exercise: european}\n";
        return o.str();
    };

    //! decreasing pillars
    CHECK_THROWS_AS( Price( quanto_with_cor(
                         "maturities: [0.75, 0.25],"
                         " matrix: [1, 0.5, 0.5, 1,  1, 0.2, 0.2, 1]" ) ),
                     std::runtime_error );
    //! non-positive-definite pillar (|rho| > 1)
    CHECK_THROWS_AS( Price( quanto_with_cor(
                         "maturities: [0.25, 0.75],"
                         " matrix: [1, 0.5, 0.5, 1,  1, 1.2, 1.2, 1]" ) ),
                     std::runtime_error );
    //! length not a multiple of the pillar count
    CHECK_THROWS_AS( Price( quanto_with_cor(
                         "maturities: [0.25, 0.75],"
                         " matrix: [1, 0.5, 0.5, 1,  1, 0.2]" ) ),
                     std::runtime_error );
}

// A "<name>_var" (spot/variance) entry may not vary across pillars: the
// stochastic-vol engines resolve it once into a scalar rho, so a term-dependent
// one would be silently ignored — the load must fail instead.
TEST_CASE( "term correlation: a term-varying <name>_var entry is rejected" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !heston_volatility {spot: 100, init_vol: 30, long_vol: 30, kappa: 2,"
      << " vol_of_vol: 0.4}\n"
      << "cor: !correlation_matrix {underlyings: [eq, eq_var],"
      << " maturities: [0.25, 0.75],"
      << " matrix: [1, -0.7, -0.7, 1,  1, -0.2, -0.2, 1]}\n"
      << "book: !book {contracts: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: 100,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call"
      << ", exercise: european}\n";
    CHECK_THROWS_AS( Price( o.str() ), std::runtime_error );
}
