#pragma once
#include "contract.hpp"

//! A ratchet (cliquet) note on one underlying: a sum of locally-capped/floored
//! periodic returns, optionally clipped globally.
//!
//! Over consecutive observation dates t_0 = today, t_1, ..., t_n = maturity
//! (anchor + k*period, maturity included), each period return
//!   R_i = S(t_i) / S(t_{i-1}) - 1
//! is clipped to [local_floor, local_cap]; the note pays at maturity
//!   nominal * clip( sum_i clip(R_i, lf, lc), global_floor, global_cap ).
//! The local cap is what makes it a "ratchet": an exceptional up-move in one
//! period is capped, but the gain is LOCKED IN (it cannot be given back by a
//! later fall), and the global floor (default 0) gives the capital protection of
//! a structured note. Local floor/cap are per-period returns in percent; the
//! global floor/cap are on the summed coupon (global_cap absent = uncapped).
//!
//! Path-dependent in the whole spot path -> Monte-Carlo only.
class Ratchet : public Contract
{

  private:
    date _maturity_date;
    double _nominal = 100;
    int _observation_period_days = 0; //!< days between period boundaries (> 0)
    double _local_floor = 0;          //!< per-period return floor (decimal; input percent)
    double _local_cap = 0;            //!< per-period return cap (decimal; input percent)
    double _global_floor = 0;         //!< coupon floor (decimal; default 0 -> capital protected)
    double _global_cap = 0;           //!< coupon cap (decimal; only if provided)
    bool _has_global_cap = false;

  public:
    //! read own fields (maturity, nominal, observation_period_days,
    //! local_floor/cap, optional global_floor/cap), then the common attributes
    void Configure( ObjectReader& reader ) override;

    //! getters
    date GetMaturityDate() const override;
    double GetNominal() const { return _nominal; }
    double LocalFloor() const { return _local_floor; }
    double LocalCap() const { return _local_cap; }
    double GlobalFloor() const { return _global_floor; }
    bool HasGlobalCap() const { return _has_global_cap; }
    double GlobalCap() const { return _global_cap; }
    //! the period-boundary schedule: today, anchor + k*period, ..., maturity
    set<date> GetObservationDates() const;

    //! path-dependent payoff — no terminal-spot intrinsic (PDE rejects it), so 0
    double Intrinsic( const double spot ) override;
    bool IsAmerican() override;

    //! mcl node: a RatchetFlowNode summing the clipped period returns
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! dates: the boundary schedule (spot fixings) and the single maturity payment
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;

    //! constructor / destructor
    Ratchet( const string& ObjectName );
    ~Ratchet() override;
};
