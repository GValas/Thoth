#pragma once
//! Shared helpers for the regression suite: in-process pricing (same path as
//! the HTTP/batch entry points), Black-Scholes references, and YAML builders.

#include "object_manager.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <yaml-cpp/yaml.h>

namespace test
{

//! price a YAML config in-process and return the parsed result tree.
//! Engine stdout is captured so the test output stays clean.
inline YAML::Node Price( const std::string& yaml, const std::string& exec = ROOT_NODE )
{
    std::streambuf* saved = std::cout.rdbuf();
    std::ostringstream sink;
    std::cout.rdbuf( sink.rdbuf() );
    std::string out;
    try
    {
        ObjectManager manager( YamlConfig::from_string_t{}, yaml );
        manager.ReadObjects( exec );
        manager.ExecuteTask();
        manager.WriteResults();
        out = manager.ResultYaml();
    }
    catch ( ... )
    {
        std::cout.rdbuf( saved );
        throw;
    }
    std::cout.rdbuf( saved );
    return YAML::Load( out );
}

//! result accessors (configs below all use result block "res")
inline double Premium( const YAML::Node& r, const std::string& opt = "" )
{
    return opt.empty() ? r["res"]["premium"].as<double>()
                       : r["res"][opt + "_premium"].as<double>();
}
inline double Trust( const YAML::Node& r, const std::string& opt = "" )
{
    return opt.empty() ? r["res"]["premium_trust"].as<double>()
                       : r["res"][opt + "_premium_trust"].as<double>();
}

//! Black-Scholes (no dividend), maturity in years
inline double NormCdf( double x ) { return 0.5 * std::erfc( -x / std::sqrt( 2.0 ) ); }

inline double BsCall( double S, double K, double r, double sig, double T )
{
    double d1 = ( std::log( S / K ) + ( r + sig * sig / 2 ) * T ) / ( sig * std::sqrt( T ) );
    double d2 = d1 - sig * std::sqrt( T );
    return S * NormCdf( d1 ) - K * std::exp( -r * T ) * NormCdf( d2 );
}
inline double BsPut( double S, double K, double r, double sig, double T )
{
    return BsCall( S, K, r, sig, T ) - S + K * std::exp( -r * T ); //!< put-call parity
}

//! closed-form quanto call: foreign asset (drift r_for, vol sig) paid in the
//! domestic currency (discount r_dom). The quanto carry is
//! b = r_for - corr(asset, FX) * sig * sig_fx, and the price is the BS forward
//! formula on F = S*exp(b*T) discounted at r_dom. corr is the underlying-vs-FX
//! correlation (FX quoted as domestic per foreign), sig/sig_fx in decimals.
inline double QuantoBsCall( double S, double K, double r_for, double r_dom,
                            double sig, double sig_fx, double corr, double T )
{
    double b = r_for - corr * sig * sig_fx;
    double F = S * std::exp( b * T );
    double d1 = ( std::log( F / K ) + 0.5 * sig * sig * T ) / ( sig * std::sqrt( T ) );
    double d2 = d1 - sig * std::sqrt( T );
    return std::exp( -r_dom * T ) * ( F * NormCdf( d1 ) - K * NormCdf( d2 ) );
}

//! the sample configs below price from today=2000-01-01 to 2000-12-31 -> T = 1y
constexpr double T1 = 1.0;

//! map the legacy integer pde precision to the current low|medium|high scheme
inline std::string PrecisionLevel( int p ) { return p <= 3 ? "low" : ( p == 4 ? "medium" : "high" ); }

//! the pricing-configuration trio (a pricer_configuration plus its referenced
//! mcl_configuration / pde_configuration sub-objects). Named cfg / cfg_mcl /
//! cfg_pde so a pricer can reference them as configuration: cfg.
inline std::string CfgBlock( const std::string& method, int draws, int max_time_step,
                             int pde_precision )
{
    std::ostringstream o;
    o << "cfg: !pricer_configuration {method: " << method
      << ", mcl_configuration: cfg_mcl, pde_configuration: cfg_pde, log_path: \"/tmp/\"}\n"
      << "cfg_mcl: !mcl_configuration {max_time_step: " << max_time_step
      << ", min_time_step: -1, paths: " << draws
      << ", vol_time_step: 0.01, use_sobol: false, use_milstein: true}\n"
      << "cfg_pde: !pde_configuration {vanilla_precision: " << PrecisionLevel( pde_precision )
      << "}\n";
    return o.str();
}

//! a single vanilla option on one equity (eur, flat rate, constant vol).
//! vol_pct / rate_pct are in percent (e.g. 30 -> 0.30). pde=true -> PDE pricer,
//! otherwise MCL. Pass method ("pde"/"mcl"/"ana") to override the selection.
inline std::string VanillaCfg( double spot, double strike, double vol_pct, double rate_pct,
                               const std::string& type, const std::string& exercise,
                               int draws, bool pde = false, int pde_precision = 5,
                               const std::string& method = "" )
{
    const std::string chosen_method = !method.empty() ? method : ( pde ? "pde" : "mcl" );
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( chosen_method, draws, 30, pde ? pde_precision : 6 )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << rate_pct << ", " << rate_pct << "]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: " << spot << ", volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: " << vol_pct << ", calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: eur, strike: " << strike
      << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: " << exercise << "}\n";
    return o.str();
}

//! a single continuously-monitored barrier option on one equity (eur, flat rate,
//! constant vol). vol_pct / rate_pct are in percent. barrier_type is one of
//! up&out / up&in / down&out / down&in. T = 1y
//! (2000-01-01 -> 2000-12-31). method is "ana" (closed form), "pde" or "mcl".
//! draws / mcl_step matter only for the mcl method (mcl_step in days drives the
//! barrier monitoring granularity).
inline std::string BarrierCfg( double spot, double strike, double barrier,
                               double vol_pct, double rate_pct,
                               const std::string& type,
                               const std::string& barrier_type,
                               const std::string& method = "ana",
                               int pde_precision = 5,
                               int draws = 1, int mcl_step = 30 )
{
    const bool is_down = barrier_type.rfind( "down", 0 ) == 0;
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: eur,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( method, draws, mcl_step, pde_precision )
      << "eur: !currency {rate: rate}\n"
      << "rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << rate_pct << ", " << rate_pct << "]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "cor: !correlation_matrix {underlyings: [eq], matrix: [1]}\n"
      << "eq: !equity {spot: " << spot << ", volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: " << vol_pct << ", calendar: cal}\n"
      << "book: !book {options: [o]}\n"
      << "o: !barrier {underlying: eq, premium_currency: eur, strike: " << strike
      << ", maturity: 2000-12-31, nominal: 1, type: " << type
      << ", barrier_type: " << barrier_type
      << ", barrier_monitoring_type: continuous_monitoring"
      << ", barrier_" << ( is_down ? "down" : "up" ) << "_level: " << barrier << "}\n";
    return o.str();
}

//! a quanto European vanilla: foreign EUR equity whose payoff is settled in USD
//! (the premium/pricer currency), with an eur/usd FX rate, FX vol, and an
//! underlying-vs-FX correlation. Mirrors the quanto cells of the pricer matrix.
//! rates / vols / corr as: r_eur_pct, r_usd_pct, vol_pct, fx_vol_pct in percent;
//! corr is the equity-vs-eur/usd correlation in [-1, 1]. method is ana/pde/mcl.
inline std::string QuantoVanillaCfg( double spot, double strike, double vol_pct,
                                     double r_eur_pct, double r_usd_pct, double fx_vol_pct,
                                     double corr, const std::string& type,
                                     const std::string& method, int draws = 1,
                                     int pde_precision = 5 )
{
    std::ostringstream o;
    o << "root: pricer\n"
      << "pricer: !pricer {today: 2000-01-01, book: book, currency: usd,"
      << " configuration: cfg, correlation: cor, indicators: [premium], result: res}\n"
      << CfgBlock( method, draws, 30, pde_precision )
      << "eur: !currency {rate: eur_rate}\n"
      << "eur_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << r_eur_pct << ", " << r_eur_pct << "]}\n"
      << "usd: !currency {rate: usd_rate}\n"
      << "usd_rate: !yield_curve {dates: [2000-01-01, 2010-01-01], values: ["
      << r_usd_pct << ", " << r_usd_pct << "]}\n"
      << "cal: !simple_weighted_calendar {non_working_days_weight: 1}\n"
      << "eq: !equity {spot: " << spot << ", volatility: vol, currency: eur}\n"
      << "vol: !bs_volatility {volatility: " << vol_pct << ", calendar: cal}\n"
      << "eur/usd: !forex {base_currency: usd, underlying_currency: eur,"
      << " spot: 1.5, volatility: fxvol}\n"
      << "fxvol: !bs_volatility {volatility: " << fx_vol_pct << "}\n"
      << "cor: !correlation_matrix {underlyings: [eq], forexs: [eur/usd],"
      << " matrix: [1, " << corr << ", " << corr << ", 1]}\n"
      << "book: !book {options: [o]}\n"
      << "o: !vanilla {underlying: eq, premium_currency: usd, strike: " << strike
      << ", is_absolute_strike: true, maturity: 2000-12-31, nominal: 1, type: " << type
      << ", exercise: european}\n";
    return o.str();
}

} // namespace test
