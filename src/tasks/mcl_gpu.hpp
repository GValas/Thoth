#pragma once
#include <string>

//! GPU (CUDA) Monte-Carlo backend for the `mcl_gpu` engine.
//!
//! Deliberately self-contained (only <string>, no project headers) so the CUDA
//! translation unit (mcl_gpu.cu) compiles under nvcc without dragging the C++23
//! engine headers through the device toolchain. Two implementations satisfy this
//! interface, selected at build time:
//!   * mcl_gpu.cu       — real CUDA kernels (built when THOTH_ENABLE_CUDA=ON)
//!   * mcl_gpu_stub.cpp — Available()==false fallback (CPU-only build)
//!
//! The pricer (PricerMCL) calls Available() once and falls back to the CPU MCL
//! engine when it is false, so a CPU-only build still runs an `!mcl_pricer` with
//! `allow_gpu: true` (transparently on the CPU).
namespace gpu
{

//! true iff Thoth was built with CUDA AND a usable device is present at runtime.
bool Available();

//! one-line description of the active device (or why the GPU is unavailable),
//! for the startup / pricing log.
std::string DeviceInfo();

//! Monte-Carlo price of a European vanilla under geometric Brownian motion, in
//! the forward measure:
//!     F_T = Forward * exp( -0.5 * Vol^2 * T + Vol * sqrt(T) * Z ),   Z ~ N(0,1)
//!     payoff = IsCall ? max(F_T - Strike, 0) : max(Strike - F_T, 0)
//!     premium = Df * mean(payoff),   trust = Df * stddev(payoff) / sqrt(Paths)
//!
//! Forward already carries the carry / dividend / quanto drift and Vol is the
//! implied vol at (Strike, maturity) — the same scalars the analytic BS pricer
//! uses, so the GPU estimate agrees with ANA/MCL within Monte-Carlo error.
//!
//! Seed seeds the per-path RNG; reusing the SAME seed for the base price and the
//! bumped re-prices gives common random numbers, so bump-and-revalue Greeks are
//! smooth rather than swamped by independent MC noise.
struct GbmResult
{
    double premium;
    double trust;
};

GbmResult PriceEuropeanGbm( double Forward, double Strike, double T, double Vol,
                            double Df, bool IsCall, long Paths, unsigned long Seed );

} // namespace gpu
