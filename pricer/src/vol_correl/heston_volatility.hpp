#pragma once
#include "volatility.hpp"

//! Heston stochastic-volatility model:
//!   dS = (r-q) S dt + sqrt(v) S dW^S
//!   dv = kappa (theta - v) dt + xi sqrt(v) dW^v,   d<W^S,W^v> = rho dt
//! Scalar (single-bucket) parameters — no term structure yet. Used by the MCL
//! engine (Andersen QE diffusion), the ANA characteristic-function pricer and
//! the 2-D PDE. GetImplicitVol returns a coarse instantaneous-vol proxy
//! (sqrt(v0)) for the few callers that still expect a single number (e.g. grid
//! bounds); the engines that understand Heston read the parameters directly.
class HestonVolatility : public Volatility
{
  protected:
    //! parameters are protected (not private) so the LSV surface — a Heston
    //! diffusion with a calibrated leverage on top — can reuse them directly
    double _spot = 0;
    double _v0 = 0;    //!< initial variance (decimal^2, e.g. 0.09 = 30% vol)
    double _kappa = 0; //!< mean-reversion speed
    double _theta = 0; //!< long-run variance
    double _xi = 0;    //!< vol-of-vol
    double _rho = 0;   //!< spot/variance correlation in (-1, 1)

    //! optional Bates jumps (all zero -> pure Heston). Lognormal jump sizes:
    //! intensity lambda (per year), log-jump mean mu and vol sigma.
    double _jump_intensity = 0;
    double _jump_mean = 0;
    double _jump_vol = 0;

  public:
    //! read own fields (Heston + optional Bates jumps), then the common calendar
    void Configure( ObjectReader& reader ) override;

    //! spot/vol correlation: set via the !correlation_matrix (the SetStochasticRho
    //! hook below), not from this object's own YAML — so it keeps a setter.
    void SetRho( double Rho ) { _rho = Rho; }

    //! getters (used by the MCL nodes and the ANA/PDE Heston pricers). The vega
    //! bump (_vol_shift) raises the whole vol level, so it shifts the variance
    //! levels v0 and theta by (sqrt(v) + shift)^2 — this makes the standard
    //! bump-and-revalue vega work for Heston too.
    //! each getter adds any vega_<param> bump (ParamShift is 0 in normal pricing).
    //! v0 / theta are variances, so the parallel vol-level vega bump goes through
    //! Shifted() while the parameter bump is additive on the variance itself.
    double GetV0() const { return Shifted( _v0 ) + ParamShift( "v0" ); }
    double GetKappa() const { return _kappa + ParamShift( "kappa" ); }
    double GetTheta() const { return Shifted( _theta ) + ParamShift( "theta" ); }
    double GetXi() const { return _xi + ParamShift( "xi" ); }
    double GetRho() const { return _rho + ParamShift( "rho" ); }
    double GetJumpIntensity() const { return _jump_intensity + ParamShift( "jump_intensity" ); }
    double GetJumpMean() const { return _jump_mean + ParamShift( "jump_mean" ); }
    double GetJumpVol() const { return _jump_vol + ParamShift( "jump_vol" ); }
    bool HasJumps() const { return _jump_intensity > 0; }

    bool IsStochastic() const override { return true; }

    //! polymorphic parameter bundle for the engines (shift-aware via the getters)
    StochasticVolParams StochasticParams() const override
    {
        return { GetV0(), GetKappa(), GetTheta(), GetXi(), GetRho(),
                 GetJumpIntensity(), GetJumpMean(), GetJumpVol() };
    }

    //! the spot/variance correlation is resolved from the global matrix at pricing
    void SetStochasticRho( double Rho ) override { SetRho( Rho ); }

    //! Heston / Bates model parameters exposed to the vega_<param> Greeks
    bool HasParam( const string& Name ) const override
    {
        return Name == "v0" || Name == "kappa" || Name == "theta" || Name == "xi" || Name == "rho" ||
               Name == "jump_intensity" || Name == "jump_mean" || Name == "jump_vol";
    }

    double GetImplicitVol( const double Strike,
                           const double Forward,
                           const date& MaturityDate ) override;

    //! Heston has no Monte-Carlo "vol node"; the diffusion is built by the
    //! engine. This returns a constant sqrt(v0) proxy for incidental callers.
    MonteCarloNode* GetNode( NodeCollector& NC ) override;

    HestonVolatility( const string& ObjectName );
    ~HestonVolatility() override;

  protected:
    //! kind-forwarding constructor for the derived LSV surface (same parameters,
    //! its own KIND_LSV_VOLATILITY tag)
    HestonVolatility( const string& ObjectName,
                      const string& ObjectKind );

    //! apply the parallel vol-level shift to a variance: (sqrt(v)+shift)^2
    double Shifted( double Variance ) const
    {
        double s = sqrt( Variance ) + _vol_shift;
        return s * s;
    }
};
