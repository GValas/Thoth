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
    long _paths;           //!< path count — 64-bit so it can exceed 2^31 (e.g. billions on GPU)
    double _vol_time_step; //!< variance sub-step (year fraction, e.g. 0.01) — a double
    string _node_file;
    bool _use_sobol;

    //! random-stream index: seeds the pseudo-random generator so cluster slaves
    //! draw independent pseudo-random paths. Default 0 (single-process pricing).
    int _seed = 0;

    //! leading Sobol points to skip — the running path count of the cluster slaves
    //! before this one, so the Sobol blocks are strictly disjoint even for an
    //! uneven split (set by the cluster master). Default 0 (single-process).
    long _sobol_skip = 0;

    //!
    MclConfiguration( const string& ObjectName );
    ~MclConfiguration() override;
};
