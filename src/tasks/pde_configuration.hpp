#pragma once
#include "object.hpp"

//! PDE grid precision level (config field "vanilla_precision": low|medium|high)
enum class Precision
{
    Low,
    Medium,
    High
};

//! default (s, t) grid sizes per Precision level, used by PdeConfiguration::Configure
//! when the field only names a preset (overridable by custom_n_s / custom_n_t)
inline constexpr int PDE_VANILLA_PRECISION_LOW_N_S = 501;
inline constexpr int PDE_VANILLA_PRECISION_LOW_N_T = 301;
inline constexpr int PDE_VANILLA_PRECISION_MEDIUM_N_S = 1001;
inline constexpr int PDE_VANILLA_PRECISION_MEDIUM_N_T = 601;
inline constexpr int PDE_VANILLA_PRECISION_HIGH_N_S = 1501;
inline constexpr int PDE_VANILLA_PRECISION_HIGH_N_T = 1301;

//! default max-stdev factor for the grid's spatial extent (custom_sigma_factor)
inline constexpr double PDE_SIGMA_FACTOR = 5.0;

//! Finite-difference (PDE) engine parameters, grouped in their own YAML object
//! (kind "pde_configuration") and referenced directly from a !pde_pricer via
//! its "pde_configuration" field (shareable across pricers).
class PdeConfiguration : public Object
{
  public:
    //! read own fields (precision preset + optional grid/sigma overrides)
    void Configure( ObjectReader& reader ) override;

    Precision _vanilla_precision; //!< low | medium | high
    int _custom_n_s;              //!< grid size for s
    int _custom_n_t;              //!< grid size for t
    double _custom_sigma_factor;  //!< max stdev factor

    //!
    PdeConfiguration( const string& ObjectName );
    ~PdeConfiguration() override;
};
