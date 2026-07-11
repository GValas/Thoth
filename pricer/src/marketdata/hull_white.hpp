#pragma once
#include "market_data.hpp"

//! hull_white.hpp — a one-factor Hull-White (extended Vasicek) short-rate model
//! attached to a !currency (field `rate_model`).
//!
//!   dr = ( theta(t) - a r ) dt + sigma_r dW_r
//!
//! The engine never needs theta(t) explicitly: it works in the factor
//! decomposition r(t) = x(t) + alpha(t) with dx = -a x dt + sigma_r dW (x(0)=0)
//! and alpha(t) fitted so the initial DISCOUNT curve is reproduced by
//! construction — which folds into the deterministic identities
//!   int_0^T alpha = z(T) T + V(T)/2,   V(T) = Var( int_0^T x ) (closed form).
//! Monte-Carlo therefore diffuses x exactly (OU step), accumulates int x by
//! trapezoid, and applies the V/2 convexity: the stochastic discount factor
//! exp(-int r) has mean P(0,T) and the equity forward is unchanged. The analytic
//! engine prices a European vanilla under BS+HW with the effective variance
//!   sigma_eff^2 T = sigma_S^2 T + 2 rho sigma_S sigma_r int B + sigma_r^2 int B^2,
//! B(t) = (1 - e^{-a (T-t)})/a — the T-forward-measure variance of S/P(t,T).
//!
//! The equity/rate correlation rho lives in the correlation matrix as the
//! pseudo-single "<currency>_ir" (the same convention as the Heston
//! "<underlying>_var" variance factor); it must be pillar-constant when the
//! matrix is term-structured. Calibration of (a, sigma_r) to swaptions is out of
//! scope: both are direct inputs.
class HullWhite : public MarketData
{
  private:
    double _mean_reversion = 0; //!< a > 0 (absolute, e.g. 0.03)
    double _volatility = 0;     //!< sigma_r (absolute; the YAML field is in percent)

  public:
    //! read own fields: mean_reversion (absolute) and volatility (percent)
    void Configure( ObjectReader& reader ) override;

    //! getters
    double A() const;     //!< mean reversion a
    double Sigma() const; //!< short-rate vol sigma_r (absolute)

    //! B(t) = (1 - e^{-a t}) / a — the HW bond-vol shape factor
    double B( double t ) const;
    //! V(t) = Var( int_0^t x du ) = (sigma^2/a^2) ( t - 2 B(t) + (1-e^{-2at})/(2a) )
    double VarIntegral( double t ) const;
    //! the rate add-on to the BS variance of a T-maturity European option:
    //!   2 rho sigma_S sigma_r int_0^T B + sigma_r^2 int_0^T B^2
    //! (add to sigma_S^2 T and divide by T for the effective implied vol)
    double EffectiveVarianceAddOn( double SigmaS, double Rho, double T ) const;

    //! constructor, destructor
    HullWhite( const string& ObjectName );
    ~HullWhite() override;
};
