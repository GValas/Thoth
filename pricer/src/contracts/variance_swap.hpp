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
    double _volatility_strike = 0;    //!< strike expressed as a volatility (decimal)
    double _notional = 1;             //!< variance notional
    int _observation_period_days = 0; //!< fixing schedule period; 0 = continuous
                                      //!< observation (every diffusion step)

  public:
    //! read own fields from the configuration (maturity / volatility_strike /
    //! notional / observation_period_days)
    void Configure( ObjectReader& reader ) override;

    //! getter
    date GetMaturityDate() const override;
    double GetVolatilityStrike() const { return _volatility_strike; }
    double GetNotional() const { return _notional; }

    //! discrete observation: realized variance is sampled on the fixing schedule
    //! today + k*period up to (and always including) maturity, instead of on every
    //! diffusion step. 0/absent keeps the continuous-observation behaviour.
    bool IsDiscretelyObserved() const { return _observation_period_days > 0; }
    set<date> GetObservationDates(); //!< the discrete fixing schedule

    //! Deterministic add-on to the continuous fair variance for a discrete fixing
    //! schedule: E[(log S_{t2}/S_{t1})^2] = var + mean^2 per interval, and the mean
    //! log-return over [t1,t2] is log(F(t2)/F(t1)) - v_fwd/2, with v_fwd the
    //! interval's FORWARD ATM implied variance sigma^2(t2) t2 - sigma^2(t1) t1 (the
    //! forward carries the full term-structured, quanto-corrected drift). Returns
    //! sum(mean_i^2)/T — exact under flat BS, a per-interval ATM approximation
    //! under a smile (matching the per-interval variance the MCL sampling realises
    //! on a term-structured surface, where a single maturity-ATM vol would not);
    //! 0 for a continuously-observed swap (the term vanishes as dt -> 0).
    double ObservationDriftVariance( const date& Today );

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
