#pragma once

//! Pricing-method capability facets for a contract.
//!
//! A contract exposes several *optional* pricing routes on top of its canonical
//! Monte-Carlo definition: a PDE grid solve, an analytic closed form and a GPU
//! Monte-Carlo kernel. Each route is its own small interface here, so a pricing
//! engine and a reader see only the slice it uses instead of one fused contract
//! interface, and adding a route is localised to a new facet. Contract inherits
//! all three; whether a given contract actually supports a route is decided at
//! run time by the *_HasSolution predicate (it depends on the underlying).
//!
//! The native MC node graph (GetFlowNode / dates) and the trade properties shared
//! across engines (Intrinsic payoff, IsAmerican exercise) stay on Contract itself
//! — they describe what the trade *is*, not an optional pricing method.

//! GPU Monte-Carlo (mcl_gpu) parameters for a European vanilla under geometric
//! Brownian motion — the forward-measure scalars (the same ones the analytic BS
//! pricer uses). Filled by GpuPriceable::GPU_GbmParams for GPU-supported contracts.
struct GpuGbmParams
{
    double forward = 0; //!< carries the carry / dividend / quanto drift
    double strike = 0;
    double t = 0;   //!< year fraction today -> maturity
    double vol = 0; //!< implied vol at (strike, maturity)
    double df = 0;  //!< discount factor to maturity
    bool is_call = true;
};

//! PDE-engine view: the barrier / variance-swap flavour flags that steer the grid
//! solve. These describe *what the trade is* (read by the PDE engine), so they live
//! on the contract; whether the grid solve *applies* is an engine decision made in
//! PricerPDE. Non-barrier, non-variance contracts keep the defaults; only the
//! relevant contract overrides each flag.
struct PdePriceable
{
    //! knock-out / knock-in barrier (continuous or discrete monitoring)
    [[nodiscard]] virtual bool PDE_IsBarrier() { return false; }
    [[nodiscard]] virtual bool PDE_IsKnockIn() { return false; }
    [[nodiscard]] virtual bool PDE_IsUpBarrier() { return false; }
    [[nodiscard]] virtual bool PDE_IsDiscreteBarrier() { return false; }
    [[nodiscard]] virtual double PDE_BarrierLevel() { return 0; }

    //! priced on the spot grid as the expected accumulated variance (a backward
    //! PDE with a local-variance source) rather than a terminal-payoff solve.
    [[nodiscard]] virtual bool PDE_IsAccruedVariance() { return false; }

    virtual ~PdePriceable() = default;
};

//! GPU Monte-Carlo view: fill Out and return true iff this contract is a
//! GPU-supported European vanilla under (deterministic-vol) GBM; false otherwise,
//! so the MCL engine falls back to the CPU path. Default: unsupported.
struct GpuPriceable
{
    [[nodiscard]] virtual bool GPU_GbmParams( GpuGbmParams& /*Out*/ ) { return false; }

    virtual ~GpuPriceable() = default;
};
