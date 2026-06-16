//! CPU-only fallback for the GPU Monte-Carlo backend. Compiled when the project
//! is built WITHOUT CUDA (THOTH_ENABLE_CUDA=OFF, the default). Available() is
//! false, so PricerMCLGpu transparently falls back to the CPU MCL engine and
//! PriceEuropeanGbm is never called; it is defined only so the link succeeds.
//!
//! The real CUDA implementation lives in mcl_gpu.cu and replaces this file in the
//! build when THOTH_ENABLE_CUDA=ON (see CMakeLists.txt).
#include "mcl_gpu.hpp"

namespace gpu
{

bool Available()
{
    return false;
}

std::string DeviceInfo()
{
    return "CUDA not built (configure with -DTHOTH_ENABLE_CUDA=ON on a CUDA host)";
}

GbmResult PriceEuropeanGbm( double, double, double, double, double, bool, long, unsigned long )
{
    //! unreachable: PricerMCLGpu gates every call on Available().
    return GbmResult{ 0.0, 0.0 };
}

} // namespace gpu
