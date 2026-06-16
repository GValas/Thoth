#pragma once
#include "pricer_mcl.hpp"

//! GPU (CUDA) Monte-Carlo engine — pricing method "mcl_gpu".
//!
//! When the whole book is GPU-supported (single-asset European vanillas under
//! GBM, see Contract::GPU_GbmParams) AND a CUDA device is present, it prices the
//! book on the device contract-by-contract, with per-contract bump-and-revalue
//! Greeks. The GPU kernel reuses the same seed for the base price and every bump,
//! so the Greeks are common-random-number differences (smooth, low variance).
//!
//! Anything else — a non-supported book, a CPU-only build, or a host without a
//! GPU — transparently falls back to the CPU MCL engine (PricerMCL): same prices,
//! just no acceleration. This keeps `method: mcl_gpu` valid everywhere.
class PricerMCLGpu : public PricerMCL
{
  public:
    PricerMCLGpu( const string& ObjectName,
                  YamlConfig& YamlConfig );
    ~PricerMCLGpu() override;

  protected:
    void PreCheck_() override;                     //!< MCL checks + decide GPU vs CPU fallback
    bool GreeksPerContract_() const override;      //!< true in GPU mode (per-contract bumps)
    void PriceBook_() override;                    //!< GPU per-contract loop, or CPU MCL fallback
    void PriceContract_( Contract* Ctr ) override; //!< one contract on the device

  private:
    bool BookIsGpuSupported_(); //!< gpu::Available() && every contract GPU_GbmParams
    bool _use_gpu = false;      //!< decided once in PreCheck_
};
