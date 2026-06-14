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
  private:
    double _spot = 0;
    double _v0 = 0;    //!< initial variance (decimal^2, e.g. 0.09 = 30% vol)
    double _kappa = 0; //!< mean-reversion speed
    double _theta = 0; //!< long-run variance
    double _xi = 0;    //!< vol-of-vol
    double _rho = 0;   //!< spot/variance correlation in (-1, 1)

  public:
    //! setters
    void SetSpot( double Spot ) { _spot = Spot; }
    void SetV0( double V0 ) { _v0 = V0; }
    void SetKappa( double Kappa ) { _kappa = Kappa; }
    void SetTheta( double Theta ) { _theta = Theta; }
    void SetXi( double Xi ) { _xi = Xi; }
    void SetRho( double Rho ) { _rho = Rho; }

    //! getters (used by the MCL nodes and the ANA/PDE Heston pricers). The vega
    //! bump (_vol_shift) raises the whole vol level, so it shifts the variance
    //! levels v0 and theta by (sqrt(v) + shift)^2 — this makes the standard
    //! bump-and-revalue vega work for Heston too.
    double GetV0() const { return Shifted( _v0 ); }
    double GetKappa() const { return _kappa; }
    double GetTheta() const { return Shifted( _theta ); }
    double GetXi() const { return _xi; }
    double GetRho() const { return _rho; }

    bool IsStochastic() const override { return true; }

    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //! Heston has no Monte-Carlo "vol node"; the diffusion is built by the
    //! engine. This returns a constant sqrt(v0) proxy for incidental callers.
    MonteCarloNode* GetNode( NodeCollector& NC ) override;

    HestonVolatility( const string& ObjectName );
    ~HestonVolatility() override;

  private:
    //! apply the parallel vol-level shift to a variance: (sqrt(v)+shift)^2
    double Shifted( double Variance ) const
    {
        double s = sqrt( Variance ) + _vol_shift;
        return s * s;
    }
};
