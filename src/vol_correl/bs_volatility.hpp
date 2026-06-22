#pragma once
#include "volatility.hpp"

//! A flat Black-Scholes volatility (one number, strike- and maturity-independent),
//! scaled by the calendar day-weight. Exact in every engine.
class BsVolatility : public Volatility
{

  private:
    //! object attributes
    double _volatility;

  public:
    //! read own field (flat vol), then the common calendar
    void Configure( ObjectReader& reader ) override;

    //! flat implied vol: ignores strike/forward/maturity, returns
    //! (vol + vega shift) * day-weight
    double GetImplicitVol( const double Strike,
                           const double Forward,
                           const date& MaturityDate ) override;

    //! MCL node: a ConstantNode carrying the same (vol + shift) * day-weight, so
    //! the simulated diffusion matches the analytic / PDE vol
    MonteCarloNode* GetNode( NodeCollector& NC ) override;

    //! constructor & destructor
    BsVolatility( const string& ObjectName );
    ~BsVolatility() override;
};
