#pragma once
#include "object.hpp"

//! PDE grid precision level (config field "vanilla_precision": low|medium|high)
enum class Precision
{
    Low,
    Medium,
    High
};

//! Finite-difference (PDE) engine parameters, grouped in their own YAML object
//! (kind "pde_configuration") and referenced from a pricer_configuration via
//! its "pde" field.
class PdeConfiguration : public Object
{
  public:
    Precision _vanilla_precision; //!< low | medium | high
    int _custom_n_s;              //!< grid size for s
    int _custom_n_t;              //!< grid size for t
    double _custom_sigma_factor;  //!< max stdev factor

    //!
    PdeConfiguration( const string& ObjectName );
    ~PdeConfiguration() override;
};
