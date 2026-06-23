#pragma once
#include "contract.hpp"

//! A variance swap: at maturity it pays notional * (realized_variance - K_var),
//! where K_var is the strike variance. Priced analytically under the flat-vol
//! model, where the expected annualized realized variance equals sigma^2, so
//!   PV = notional * DF * (sigma^2 - K_var).
//! The payoff is path-dependent in the spot (no terminal-spot PDE grid); a
//! Monte-Carlo node is not wired for it yet, so it is priced via method:
//! ana.
class VarianceSwap : public Contract
{

  private:
    date _maturity_date;
    double _volatility_strike = 0; //!< strike expressed as a volatility (decimal)
    double _notional = 1;          //!< variance notional

  public:
    //! read own fields from the configuration (maturity / volatility_strike / notional)
    void Configure( ObjectReader& reader ) override;

    //! getter
    date GetMaturityDate() const override;
    double GetVolatilityStrike() const { return _volatility_strike; }
    double GetNotional() const { return _notional; }

    //! mcl node (not supported)
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! trade properties (no terminal spot payoff; European)
    double Intrinsic( const double spot ) override;
    bool IsAmerican() override;

    //! dates (single payment at maturity)
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;

    //! constructor / destructor
    VarianceSwap( const string& ObjectName );
    ~VarianceSwap() override;
};
