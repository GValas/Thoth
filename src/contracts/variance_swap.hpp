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
    //! setter
    void SetMaturityDate( const date& MaturityDate );
    void SetVolatilityStrike( double VolatilityStrike );
    void SetNotional( double Notional );

    //! getter
    date GetMaturityDate() override;

    //! mcl node (not supported)
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! pde (path-dependent in spot -> no grid solution)
    bool PDE_HasSolution() override;
    double PDE_EvalFlow( const double spot ) override;
    bool PDE_IsAmerican() override;

    //! analytical (flat-vol fair variance)
    bool ANA_HasSolution() override;
    void ANA_EvalPrice() override;

    //! dates (single payment at maturity)
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;
    set<date> GetAmericanExerciseDates() override;

    //! constructor / destructor
    VarianceSwap( const string& ObjectName );
    ~VarianceSwap() override;
};
