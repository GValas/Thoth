#pragma once
#include "volatility.hpp"

//! SABR implied-volatility surface (Hagan 2002 lognormal approximation) with
//! arbitrage-free wings.
//!
//! Per-maturity parameters (alpha, beta, rho, nu) are linearly interpolated in
//! time (flat beyond the ends); the forward is supplied by the caller (the owning
//! underlying's forward to the maturity). Like the model it replaces, this is an
//! implied surface used as a local-vol surface (Dupire) by the PDE pricer; it has
//! no Monte-Carlo node.
//!
//! Hagan's expansion develops butterfly arbitrage (negative implied density) in
//! the far wings, which used to surface as a negative Dupire local variance that
//! GetLocalVolatility silently floored. Beyond a +/-SABR_WING_SIGMA_CUTOFF
//! log-moneyness band the surface therefore switches to Benaim-Dodgson-Kainth
//! power-law price tails matched in value AND slope to the Hagan prices at the
//! cutoff: an undiscounted call c(K) = c+ * (K+/K)^a on the right, a put
//! p(K) = p- * (K/K-)^b on the left. Both tails have a strictly positive density
//! by construction (c'' = a(a+1)c/K^2, p'' = b(b-1)p/K^2 with b > 1), and the C1
//! price match makes the implied vol continuous with a continuous strike slope at
//! the junction. Wing implied vols are recovered from the tail prices by a
//! relative-tolerance bisection (robust down to the tiny far-wing premia).
class SabrVolatility : public Volatility
{

  private:
    vector<double> _maturity_list; //!< in years, strictly ascending
    vector<double> _alpha_list;    //!< overall level (decimals, e.g. 0.2 = 20%)
    vector<double> _beta_list;     //!< CEV exponent in [0, 1]
    vector<double> _rho_list;      //!< spot/vol correlation in (-1, 1)
    vector<double> _nu_list;       //!< vol-of-vol (decimals)

    //! linear interpolation (flat beyond the ends) of a per-maturity parameter
    double Interp( const vector<double>& Values, double T ) const;

    //! the raw Hagan 2002 expansion at (K, F, T) — the surface INSIDE the wing
    //! cutoffs, and the source of the cutoff prices/slopes the tails are matched to
    double HaganVol( double K, double F, double T ) const;

    //! implied vol of the arbitrage-free power-law tail at K (right wing when
    //! K > cutoff, left wing when K < cutoff). Falls back to the Hagan vol when
    //! the tail is not admissible (left-wing exponent b <= 1).
    double WingVol( double K, double F, double T, double Cutoff, bool Right ) const;

  public:
    //! read own fields (per-maturity SABR params), then the common calendar
    void Configure( ObjectReader& reader ) override;

    //! Hagan-2002 lognormal SABR implied vol at (Strike, Forward, MaturityDate);
    //! Strike <= 0 is a sentinel meaning "ATM" (use the forward as strike)
    double GetImplicitVol( const double Strike,
                           const double Forward,
                           const date& MaturityDate ) override;

    //! SABR model parameters exposed to the vega_<param> Greeks
    bool HasParam( const string& Name ) const override
    {
        return Name == "alpha" || Name == "beta" || Name == "rho" || Name == "nu";
    }

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC ) override;

    //! constructor, destructor
    SabrVolatility( const string& ObjectName );
    ~SabrVolatility() override;
};
