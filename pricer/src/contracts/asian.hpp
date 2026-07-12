#pragma once
#include "contract.hpp"
#include "enums.hpp"

//! An arithmetic-average (average-price) Asian option on one underlying.
//!
//! At maturity it pays nominal * max( omega * (A - K), 0 ), with omega = +1 for a
//! call / -1 for a put, K the strike, and A the ARITHMETIC MEAN of the spot over
//! the observation schedule (anchor today + k*period, up to and including
//! maturity). Averaging damps the terminal-spot variance, so an average-price
//! option is cheaper than the equivalent vanilla.
//!
//! Path-dependent in the whole spot path -> Monte-Carlo only (the 1-D PDE grid
//! would need the running average as an extra state dimension, and there is no
//! exact closed form for the arithmetic average). The strike is booked absolute
//! or as a percent of the valuation-date spot (relative), resolved once in
//! SetToday like a vanilla.
class Asian : public Contract
{

  private:
    date _maturity_date;
    OptionType _type = OptionType::Call;
    double _strike_input = 0;         //!< configured strike (cash, or % of spot)
    bool _is_absolute_strike = true;  //!< false: strike is a percent of the spot
    double _strike = 0;               //!< resolved cash strike (see SetToday)
    double _nominal = 1;              //!< payoff notional
    int _observation_period_days = 0; //!< days between averaging fixings (> 0)

  public:
    //! read own fields (maturity, type, strike/is_absolute_strike, nominal,
    //! observation_period_days), then the common contract attributes
    void Configure( ObjectReader& reader ) override;
    //! resolve a relative strike against the valuation-date spot (idempotent under
    //! the theta roll; never re-anchored by a Greek bump — sticky cash)
    void SetToday( const date& Today ) override;

    //! getters
    date GetMaturityDate() const override;
    OptionType GetType() const { return _type; }
    double GetStrike() const { return _strike; }
    double GetNominal() const { return _nominal; }
    //! the averaging schedule: anchor + k*period (k >= 1) up to and including maturity
    set<date> GetObservationDates() const;

    //! path-dependent payoff — no terminal-spot intrinsic (the PDE rejects it), so 0
    double Intrinsic( const double spot ) override;
    bool IsAmerican() override;

    //! mcl node: an AsianFlowNode averaging the spot over the schedule
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! dates: the observation schedule (spot fixings) and the single maturity payment
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;

    //! constructor / destructor
    Asian( const string& ObjectName );
    ~Asian() override;
};
