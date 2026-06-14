#pragma once
#include "volatility.hpp"

//! SABR implied-volatility surface (Hagan 2002 lognormal approximation).
//!
//! Per-maturity parameters (alpha, beta, rho, nu) are linearly interpolated in
//! time (flat beyond the ends); the forward is approximated by the spot. Like
//! the model it replaces, this is an implied surface used as a local-vol
//! surface (Dupire) by the PDE pricer; it has no Monte-Carlo node.
class SabrVolatility : public Volatility
{

  private:
    double _spot = 0;
    vector<double> _maturity_list; //!< in years, strictly ascending
    vector<double> _alpha_list;    //!< overall level (decimals, e.g. 0.2 = 20%)
    vector<double> _beta_list;     //!< CEV exponent in [0, 1]
    vector<double> _rho_list;      //!< spot/vol correlation in (-1, 1)
    vector<double> _nu_list;       //!< vol-of-vol (decimals)

    //! linear interpolation (flat beyond the ends) of a per-maturity parameter
    double Interp( const vector<double>& Values, double T ) const;

  public:
    //! setters
    void SetSpot( double Spot );
    void SetMaturityList( const vector<double>& MaturityList );
    void SetAlphaList( const vector<double>& AlphaList );
    void SetBetaList( const vector<double>& BetaList );
    void SetRhoList( const vector<double>& RhoList );
    void SetNuList( const vector<double>& NuList );

    //!
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC ) override;

    //! constructor, destructor
    SabrVolatility( const string& ObjectName );
    ~SabrVolatility() override;
};
