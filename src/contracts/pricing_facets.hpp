#pragma once

//! Contract description flags read by a pricing engine.
//!
//! These describe *what the trade is* (e.g. its barrier flavour) so the engine that
//! reads them can steer its solve. They are NOT engine decisions: whether a given
//! engine can price a contract (a PDE grid / a closed form / a GPU GBM kernel) is
//! decided in the engine itself (PricerPDE / PricerANA / PricerMCL), which inspects
//! the contract + its underlying.
//!
//! The native MC node graph (GetFlowNode / dates) and the trade properties shared
//! across engines (Intrinsic payoff, IsAmerican exercise) stay on Contract itself.

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
