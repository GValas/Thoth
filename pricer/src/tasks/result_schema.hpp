#pragma once
#include <string>

//! result_schema.hpp — the canonical field names of a pricer result block, shared
//! by the producer (Task/Pricer::WriteResults) and the cluster aggregator
//! (run_cluster.cpp pooling rules). This is the single place the wire schema is
//! spelled: a new field added through these helpers pools correctly on the cluster
//! without touching the master, and a renamed field cannot silently diverge
//! between producer and aggregator (the historical failure mode: a hardcoded
//! {delta..theta} list in the aggregator dropped every vega_<param>).
//!
//! Consumers outside the process (tests, the web BFF, scripts) read the same names
//! off the YAML wire; treat any change here as a wire-format change.
namespace result_schema
{

//! --- fixed fields -----------------------------------------------------------
inline const std::string KIND = "kind";           //!< result-block kind tag (string)
inline const std::string TASK_TIME = "task_time"; //!< wall time, NOT poolable (cluster: max)

inline const std::string PREMIUM = "premium";
inline const std::string DELTA = "delta";
inline const std::string GAMMA = "gamma";
inline const std::string VEGA = "vega";
inline const std::string RHO = "rho";
inline const std::string THETA = "theta";

//! --- derived-name rules -------------------------------------------------------
//! standard error of a value field: "<field>_trust" (cluster pools it in quadrature)
inline const std::string TRUST_SUFFIX = "_trust";
inline const std::string PREMIUM_TRUST = PREMIUM + TRUST_SUFFIX;

//! model-parameter Greeks: "vega_<param>" (vega_alpha, vega_v0, vega_jump_vol, ...)
inline const std::string PARAM_VEGA_PREFIX = "vega_";
inline std::string ParamVega( const std::string& Param )
{
    return PARAM_VEGA_PREFIX + Param;
}

//! per-contract fields: "<contract>_<metric>" (e.g. "my_call_premium")
inline std::string ContractField( const std::string& Contract, const std::string& Metric )
{
    return Contract + "_" + Metric;
}

//! true for the "<field>_trust" standard-error fields (quadrature pooling)
inline bool IsTrust( const std::string& Key )
{
    return Key.size() >= TRUST_SUFFIX.size() &&
           Key.compare( Key.size() - TRUST_SUFFIX.size(), TRUST_SUFFIX.size(), TRUST_SUFFIX ) == 0;
}

} // namespace result_schema
