#pragma once
#include "object.hpp"

//! Monte-Carlo (Longstaff-Schwartz) engine parameters, grouped in their own
//! YAML object (kind "mcl_configuration") and referenced from a
//! pricer_configuration via its "mcl" field.
class MclConfiguration : public Object
{
  public:
    int _max_time_step;
    int _min_time_step;
    int _paths;
    int _vol_time_step;
    string _node_file;
    bool _use_sobol;
    bool _use_milstein;

    //! random-stream index: seeds the pseudo-random generator and offsets the
    //! Sobol sequence by _seed * _paths points, so cluster slaves draw disjoint,
    //! independent paths. Default 0 (single-process pricing).
    int _seed = 0;

    //!
    MclConfiguration( const string& ObjectName );
    ~MclConfiguration() override;
};
