#pragma once
#include "heston_volatility.hpp"

//! Local-stochastic volatility (LSV): a Heston variance factor whose spot
//! diffusion is multiplied by a leverage function L(S,t),
//!   dS = (r-q) S dt + L(S,t) sqrt(v) S dW^S
//!   dv = kappa (theta - v) dt + xi sqrt(v) dW^v,   d<W^S,W^v> = rho dt
//! with L calibrated so the model reprices a TARGET implied surface exactly
//! (Dupire matching: L^2(s,t) * E[v_t | S_t = s] = sigma_dupire^2(s,t)).
//!
//! This object holds the Heston parameters (inherited) plus a reference to the
//! target surface (any deterministic Volatility — typically SABR, or flat BS).
//! GetImplicitVol delegates to the target: by construction the calibrated model's
//! vanilla surface IS the target surface, so every implied-vol consumer (quanto
//! drift, grid bounds, Dupire) reads the right numbers. The leverage itself is
//! calibrated by the engines (Single::CalibrateLeverage, a binned particle
//! method) — MCL applies it in the spot diffusion node, the PDE in its 2-D ADI
//! coefficients; ANA has no closed form and rejects the model.
//!
//! Like Heston, the spot/variance correlation rho comes from the global
//! !correlation_matrix (keyed "<underlying>_var"). Bates jumps are NOT supported
//! under LSV (the Dupire matching would have to strip the jump contribution).
class LsvVolatility : public HestonVolatility
{
  private:
    Volatility* _surface = nullptr; //!< target implied surface (non-owning book object)

  public:
    //! read the Heston fields, then the target-surface reference; rejects jumps
    void Configure( ObjectReader& reader ) override;

    //! date this object AND the target surface (referenced only from here, so the
    //! usual underlying -> volatility dating chain would not reach it otherwise)
    void SetToday( const date& Today ) override;

    bool IsLsv() const override { return true; }

    //! the calibrated model reprices the target surface, so the model's implied
    //! vol IS the target's (plus the parallel vega bump). Dupire local vol — the
    //! calibration target — follows from this via the base GetLocalVolatility.
    double GetImplicitVol( const double Strike,
                           const double Forward,
                           const date& MaturityDate ) override;

    //! Heston parameters WITHOUT the parallel vol-level shift: the vega bump moves
    //! the target surface (through GetImplicitVol above) and the leverage
    //! recalibrates against it — also shifting v0/theta would double-count the
    //! bump. The per-parameter vega_<param> bumps still apply (they measure the
    //! smile-dynamics sensitivity at a fixed vanilla surface).
    StochasticVolParams StochasticParams() const override
    {
        return { _v0 + ParamShift( "v0" ), _kappa + ParamShift( "kappa" ),
                 _theta + ParamShift( "theta" ), _xi + ParamShift( "xi" ),
                 _rho + ParamShift( "rho" ), 0, 0, 0 };
    }

    //! LSV model parameters exposed to the vega_<param> Greeks (no jumps)
    bool HasParam( const string& Name ) const override
    {
        return Name == "v0" || Name == "kappa" || Name == "theta" || Name == "xi" || Name == "rho";
    }

    LsvVolatility( const string& ObjectName );
    ~LsvVolatility() override;
};
