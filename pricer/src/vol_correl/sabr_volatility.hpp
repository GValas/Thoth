#pragma once
#include "volatility.hpp"

//! SABR implied-volatility surface (Hagan 2002 lognormal approximation).
//!
//! Per-maturity parameters (alpha, beta, rho, nu) are linearly interpolated in
//! time (flat beyond the ends); the forward is supplied by the caller (the owning
//! underlying's forward to the maturity). Like the model it replaces, this is an
//! implied surface used as a local-vol surface (Dupire) by the PDE pricer; it has
//! no Monte-Carlo node.
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
