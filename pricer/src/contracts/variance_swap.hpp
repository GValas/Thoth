#pragma once
#include "contract.hpp"

class SimpleFixingData;

//! A variance swap: at maturity it pays notional * (realized_variance - K_var),
//! where K_var is the strike variance and the realized variance is annualized
//! over the whole observation window.
//!
//! Seasoned (in-life) swaps: an optional `start` date in the past opens the
//! window before today; the already-observed leg is read from a
//! !simple_fixing_data (`fixings`) — squared log-returns over the past fixings,
//! plus the bridge log^2(spot / last_fixing) from the last observation to
//! today — and every engine prices the remaining leg from today, time-weighting
//! the two:  fair_total = ( past_sum2 + fair_future * T_future ) / T_total.
//! Splitting the current interval at today drops the (drift-level) cross term
//! 2 log(S/F_last) E[log(S_next/S)] — the standard mid-life convention, which is
//! also exactly what the MC path (restarting at today's spot) realises, so the
//! three engines stay consistent. Without `start`, the historic spot-started
//! behaviour is unchanged.
class VarianceSwap : public Contract
{

  private:
    date _maturity_date;
    double _volatility_strike = 0;    //!< strike expressed as a volatility (decimal)
    double _notional = 1;             //!< variance notional
    int _observation_period_days = 0; //!< fixing schedule period; 0 = continuous
                                      //!< observation (every diffusion step)
    //! seasoned swaps: observation start in the past + the realised fixings
    date _start_date;
    bool _has_start = false;
    SimpleFixingData* _fixings = nullptr; //!< past observations (non-owning ref)
    //! the earliest valuation date seen (SetToday keeps the minimum): the realised
    //! path is frozen there, so a schedule date the THETA ROLL pushes into the past
    //! fixes at the (held) live spot instead of demanding an impossible fixing
    date _anchor_today;
    bool _anchor_today_set = false;

    //! the validated past observations (date, value) in date order: the exact
    //! schedule for a discrete swap, the provided fixings in [start, today) for a
    //! continuous one (fixings dated today or later are superseded by the spot)
    vector<std::pair<date, double>> PastObservations();

  public:
    //! read own fields from the configuration (maturity / volatility_strike /
    //! notional / observation_period_days)
    void Configure( ObjectReader& reader ) override;

    //! getter
    date GetMaturityDate() const override;
    double GetVolatilityStrike() const { return _volatility_strike; }
    double GetNotional() const { return _notional; }

    //! discrete observation: realized variance is sampled on the fixing schedule
    //! anchor + k*period up to (and always including) maturity — anchor = start
    //! for a seasoned swap, today otherwise — instead of on every diffusion step.
    //! 0/absent keeps the continuous-observation behaviour. Only FUTURE dates are
    //! returned (the past leg is read from the fixings, not diffused).
    bool IsDiscretelyObserved() const { return _observation_period_days > 0; }
    set<date> GetObservationDates(); //!< the remaining (future) fixing schedule

    //! seasoned (in-life) swap: a start date strictly before today
    bool IsSeasoned() const { return _has_start && _start_date < _today; }
    //! reject a forward start as soon as the valuation date is known
    void SetToday( const date& Today ) override;
    //! annualizer of the total window: YearFraction(start-or-today, maturity)
    double GetTotalYearFraction() const;
    //! the realised leg: sum of squared log-returns over the past fixings
    //! (validated against the observation schedule) plus the last-fixing -> spot
    //! bridge; 0 for a spot-started swap
    double PastSumSquaredReturns();
    //! the bridge log-return log( spot / last_past_fixing ) — the only
    //! spot-sensitive term of the realised leg (analytic delta/gamma); 0 unseasoned
    double LastFixingLogBridge();

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
