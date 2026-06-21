#pragma once
#include "object.hpp"

//! defaults read by MclConfiguration::Configure for the optional fields
inline constexpr bool MC_USE_SOBOL = true;  //!< Sobol (vs pseudo-random) by default
inline constexpr char MCL_NODE_PATH[] = ""; //!< node-dump file (empty: no dump)

//! Monte-Carlo (Longstaff-Schwartz) engine parameters, grouped in their own
//! YAML object (kind "mcl_configuration") and referenced from a
//! pricer_configuration via its "mcl" field.
class MclConfiguration : public Object
{
  public:
    //! read own fields (grid steps, path count, Sobol / GPU switches) with guards
    void Configure( ObjectReader& reader ) override;

    int _max_day_step;
    int _min_day_step;
    long _paths;           //!< path count — 64-bit so it can exceed 2^31 (e.g. billions on GPU)
    double _vol_year_step; //!< variance sub-step (year fraction, e.g. 0.01) — a double
    string _node_file;
    bool _use_sobol;

    //! random-stream index: seeds the pseudo-random generator so cluster slaves
    //! draw independent pseudo-random paths. Default 0 (single-process pricing).
    int _seed = 0;

    //! leading Sobol points to skip — the running path count of the cluster slaves
    //! before this one, so the Sobol blocks are strictly disjoint even for an
    //! uneven split (set by the cluster master). Default 0 (single-process).
    long _sobol_skip = 0;

    //! opt in to GPU (CUDA) acceleration: when true AND a usable device is present
    //! AND the whole book is GPU-supported (single-asset European vanillas under
    //! GBM), the MCL engine prices on the device; otherwise it transparently runs
    //! on the CPU. Default false. Auto-enabled by the legacy method "mcl_gpu".
    bool _allow_gpu = false;

    //!
    MclConfiguration( const string& ObjectName );
    ~MclConfiguration() override;
};
