#include "helpers.hpp"
#include <doctest/doctest.h>

using namespace test;

// Cross-engine agreement: for a plain European vanilla (no dividend, b = r) the
// analytic, PDE and Monte-Carlo engines must all converge to the same price.
TEST_CASE( "ANA, PDE and MCL agree on European vanillas" )
{
    const double S = 100, r = 0.06;
    const double vol = 30;

    for ( double K : { 80.0, 100.0, 120.0 } )
    {
        for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
        {
            CAPTURE( K );
            CAPTURE( type );

            double bs = ( type == "call" )
                            ? BsCall( S, K, r, vol / 100, T1 )
                            : BsPut( S, K, r, vol / 100, T1 );

            double ana = Premium( Price( VanillaCfg( S, K, vol, r * 100, type, "european",
                                                     1, false, 5, "ana" ) ) );
            double pde = Premium( Price( VanillaCfg( S, K, vol, r * 100, type, "european",
                                                     1, true, 5 ) ) );
            auto mr = Price( VanillaCfg( S, K, vol, r * 100, type, "european", 80000 ) );
            double mcl = Premium( mr );

            CHECK( std::abs( ana - bs ) <= 1e-2 );                     //!< closed form
            CHECK( std::abs( pde - bs ) <= 0.05 );                     //!< grid discretisation
            CHECK( std::abs( mcl - bs ) <= 6.0 * Trust( mr ) + 1e-2 ); //!< MC error
            CHECK( std::abs( ana - pde ) <= 0.05 );                    //!< engines agree
        }
    }
}

// Cross-engine agreement on quanto European vanillas: a EUR asset paid in USD.
// All three engines apply the quanto drift correction
//   b = r_eur - corr(asset, eur/usd) * sigma_S * sigma_X
// and discount in USD, so they must converge to the closed-form quanto price.
TEST_CASE( "ANA, PDE and MCL agree on quanto European vanillas" )
{
    const double S = 100, r_eur = 8, r_usd = 5, vol = 30, fx_vol = 15;

    for ( double K : { 80.0, 100.0, 120.0 } )
    {
        for ( double corr : { -0.5, 0.0, 0.5 } )
        {
            for ( std::string type : { std::string( "call" ), std::string( "put" ) } )
            {
                CAPTURE( K );
                CAPTURE( corr );
                CAPTURE( type );

                //! closed-form quanto reference (put via the quanto put-call parity:
                //! call - put = df_usd * (F - K), with F = S*exp(b*T))
                double call = QuantoBsCall( S, K, r_eur / 100, r_usd / 100,
                                            vol / 100, fx_vol / 100, corr, T1 );
                double b = r_eur / 100 - corr * ( vol / 100 ) * ( fx_vol / 100 );
                double ref = ( type == "call" )
                                 ? call
                                 : call - std::exp( -r_usd / 100 * T1 ) *
                                              ( S * std::exp( b * T1 ) - K );

                double ana = Premium( Price( QuantoVanillaCfg( S, K, vol, r_eur, r_usd, fx_vol,
                                                               corr, type, "ana" ) ) );
                double pde = Premium( Price( QuantoVanillaCfg( S, K, vol, r_eur, r_usd, fx_vol,
                                                               corr, type, "pde" ) ) );
                auto mr = Price( QuantoVanillaCfg( S, K, vol, r_eur, r_usd, fx_vol,
                                                   corr, type, "mcl", 80000 ) );
                double mcl = Premium( mr );

                CHECK( std::abs( ana - ref ) <= 1e-2 );                     //!< closed form
                CHECK( std::abs( pde - ref ) <= 0.05 );                     //!< grid discretisation
                CHECK( std::abs( mcl - ref ) <= 6.0 * Trust( mr ) + 1e-2 ); //!< MC error
                CHECK( std::abs( ana - pde ) <= 0.05 );                     //!< engines agree
            }
        }
    }
}

// The book total equals the sum of its contract premiums (analytic, exact).
TEST_CASE( "book premium is the sum of its contracts" )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !ana_pricer {today: 2000-01-01, book: book, currency: eur,"
      << " correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( 1, 30, 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [5, 5]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: 30, calendar: cal}\n"
      << "c1: !vanilla {underlying: eq, premium_currency: eur, strike: 90,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: call, exercise: european}\n"
      << "c2: !vanilla {underlying: eq, premium_currency: eur, strike: 110,"
      << " is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: put, exercise: european}\n"
      << "book: !book {contracts: [c1, c2]}\n";

    auto res = Price( o.str() );
    double total = Premium( res );
    double sum = Premium( res, "c1" ) + Premium( res, "c2" );
    CHECK( total == doctest::Approx( sum ).epsilon( 1e-9 ) );
}

// --- quanto + SABR: the drift correction convention is pinned to the ATM implied
// vol in ALL THREE engines. ANA applies it on the forward (Single::GetForward),
// PDE as a carry spread (InitGrid, rho*v_atm*v_fx with the local-vol grid
// underneath), and MCL wires the QuantoAdjustmentNode with Single::GetVolNode —
// which for a local (SABR) surface deliberately returns a CONSTANT ATM vol node,
// not the instantaneous local vol (see single.cpp GetVolNode). This test pins that
// three-way agreement so a change of convention in any one engine shows up.
TEST_CASE( "quanto vanilla on a SABR surface agrees across ANA/MCL/PDE" )
{
    auto cfg = []( const std::string& method, double strike, int draws )
    {
        std::ostringstream o;
        o << "root: pricer\n"
          << "pricer: !" << method << "_pricer {today: 2000-01-01, book: book, currency: usd,"
          << ConfigRef( method ) << " correlation: cor, indicators: [premium], result: res}\n"
          << CfgBlock( draws, 7, 5 )
          << "eur: !currency {rate: eur_rate}\n"
          << "eur_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [3, 3]}\n"
          << "usd: !currency {rate: usd_rate}\n"
          << "usd_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: [6, 6]}\n"
          << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
          << "eq: !equity {spot: 100, volatility: vol, currency: eur}\n"
          //! genuine smile (rho -0.3, nu 0.4) so the local-vol machinery is live
          << "vol: !sabr_volatility {maturities: [1.0], alpha: [0.30], beta: [1.0],"
          << " rho: [-0.3], nu: [0.4], calendar: cal}\n"
          << "eur/usd: !forex {base_currency: usd, underlying_currency: eur,"
          << " spot: 1.5, volatility: fxvol}\n"
          << "fxvol: !bs_volatility {volatility: 12}\n"
          << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd],"
          << " matrix: [1, 0.4, 0.4, 1]}\n"
          << "book: !book {contracts: [o]}\n"
          << "o: !vanilla {underlying: eq, premium_currency: usd, strike: " << strike
          << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1,"
          << " type: call, exercise: european}\n";
        return o.str();
    };

    for ( double strike : { 100.0, 120.0 } )
    {
        CAPTURE( strike );
        const double ana = Premium( Price( cfg( "ana", strike, 1 ) ) );
        const double pde = Premium( Price( cfg( "pde", strike, 1 ) ) );
        auto mr = Price( cfg( "mcl", strike, 200000 ) );
        const double mcl = Premium( mr );
        CAPTURE( ana );
        CAPTURE( pde );
        CAPTURE( mcl );
        //! ANA prices off the implied vol AT THE STRIKE, the PDE and MCL diffuse the
        //! Dupire local vol — Dupire repricing makes them agree; the shared ATM
        //! quanto correction keeps the drift identical across the three.
        CHECK( std::abs( pde - ana ) <= 0.015 * ana + 0.05 );
        CHECK( std::abs( mcl - ana ) <= 6.0 * Trust( mr ) + 0.015 * ana + 0.05 );
    }
}
