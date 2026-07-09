#pragma once
#include "contract.hpp"

//! A barrier option: knock-in / knock-out, up / down, continuous or discrete
//! monitoring. Priced by PDE and MCL, and (continuous only) by the closed form;
//! knock-in is obtained from knock-out by in/out parity (vanilla = in + out).
//! The terminal payoff is the underlying vanilla; the barrier feature only governs
//! whether that payoff survives to maturity (knock-out) or activates (knock-in).
class Barrier : public Contract
{
  public:
    //! barrier definition (public: the PDE/MCL engines read these directly)
    double _barrier_up_level = 0;                                               //!< H for an up barrier (0 = unused)
    double _barrier_down_level = 0;                                             //!< H for a down barrier (0 = unused)
    BarrierType _barrier_type = BarrierType::UpAndOut;                          //!< up/down x in/out
    BarrierMonitoring _barrier_monitoring_type = BarrierMonitoring::Continuous; //!< continuous vs discrete
    int _monitoring_period_days = 0;                                            //!< discrete monitoring step (days); 0 = unset
    date _maturity_date;
    double _strike = 0; //!< resolved cash strike K of the terminal payoff (see SetToday)
    OptionType _type = OptionType::Call;

  private:
    double _strike_input = 0;        //!< configured strike (cash, or % of spot)
    bool _is_absolute_strike = true; //!< false: strike is a percent of the spot

  public:
    //! discrete monitoring helpers — true iff the barrier is only checked on a
    //! scheduled grid (vs every instant for continuous monitoring)
    bool IsDiscrete() const
    {
        return _barrier_monitoring_type == BarrierMonitoring::Discrete;
    }
    //! read own fields from the configuration (strike / maturity / type / barrier)
    void Configure( ObjectReader& reader ) override;

    //! roll the valuation date, then resolve a relative strike against the base
    //! (unbumped) spot so the cash strike stays fixed through Greek bumps. The
    //! barrier levels are always absolute.
    void SetToday( const date& Today ) override;

    //! monitoring schedule: today + k*period (k>=1) up to and including maturity
    set<date> GetMonitoringDates();

    //! mcl node — a BarrierFlowNode (vanilla payoff gated by the monitored barrier)
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! getter
    date GetMaturityDate() const override;

    //! trade properties (intrinsic payoff + exercise style)
    double Intrinsic( const double Spot ) override;
    //! barriers are European-style here: no early exercise
    bool IsAmerican() override
    {
        return false;
    }

    //! barrier flavour read by the engines that price it (PDE grid / MCL flow node):
    //! up vs down, knock-in vs knock-out, and the active level H. Continuous vs
    //! discrete monitoring is IsDiscrete() above.
    [[nodiscard]] bool IsUp() const;
    [[nodiscard]] bool IsIn() const;
    [[nodiscard]] double Level() const;

    //! fixing dates: maturity, plus every monitoring date when discretely monitored
    set<date> GetFixingDates() override;
    //! flow dates: the single settlement at maturity
    set<date> GetFlowDates() override;

    //! constructeur / destructeur
    Barrier( const string& ObjectName );
    ~Barrier() override;
};
