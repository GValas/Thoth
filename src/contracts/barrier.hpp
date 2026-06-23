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
    double _strike = 0; //!< vanilla strike K of the terminal payoff
    OptionType _type = OptionType::Call;

    //! discrete monitoring helpers — true iff the barrier is only checked on a
    //! scheduled grid (vs every instant for continuous monitoring)
    bool IsDiscrete() const
    {
        return _barrier_monitoring_type == BarrierMonitoring::Discrete;
    }
    //! read own fields from the configuration (strike / maturity / type / barrier)
    void Configure( ObjectReader& reader ) override;

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

    //! pde — a grid solve is available iff the underlying is griddable
    bool PDE_HasSolution() override;

    //! pde barrier flags steering the grid solve (continuous: Dirichlet boundary;
    //! discrete: zero the knocked region at scheduled dates; knock-in by parity)
    bool PDE_IsBarrier() override;
    bool PDE_IsKnockIn() override;
    bool PDE_IsUpBarrier() override;
    bool PDE_IsDiscreteBarrier() override
    {
        return IsDiscrete();
    }
    //! the active barrier level H (up vs down depending on the type)
    double PDE_BarrierLevel() override;

    //! analytical — a Reiner-Rubinstein closed form exists (continuous monitoring
    //! only); the formula itself lives in PricerANA.
    bool ANA_HasSolution() override;

    //! fixing dates: maturity, plus every monitoring date when discretely monitored
    set<date> GetFixingDates() override;
    //! flow dates: the single settlement at maturity
    set<date> GetFlowDates() override;

    //! constructeur / destructeur
    Barrier( const string& ObjectName );
    ~Barrier() override;
};
