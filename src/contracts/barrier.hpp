#pragma once
#include "contract.hpp"

//! A barrier option: knock-in / knock-out, up / down, continuous or discrete
//! monitoring. Priced by PDE and MCL, and (continuous only) by the closed form;
//! knock-in is obtained from knock-out by in/out parity (vanilla = in + out).
class Barrier : public Contract
{
  public:
    //
    double _barrier_up_level = 0;
    double _barrier_down_level = 0;
    BarrierType _barrier_type = BarrierType::UpAndOut;
    BarrierMonitoring _barrier_monitoring_type = BarrierMonitoring::Continuous;
    int _monitoring_period_days = 0; //!< discrete monitoring step (days); 0 = unset
    date _maturity_date;
    double _strike = 0;
    OptionType _type = OptionType::Call;

    //! discrete monitoring helpers
    bool IsDiscrete() const
    {
        return _barrier_monitoring_type == BarrierMonitoring::Discrete;
    }
    //! monitoring schedule: today + k*period (k>=1) up to and including maturity
    set<date> GetMonitoringDates();

    //! mcl node
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! getter
    date GetMaturityDate() override;

    //! trade properties (intrinsic payoff + exercise style)
    double Intrinsic( const double Spot ) override;
    bool IsAmerican() override
    {
        return false;
    }

    //! pde
    bool PDE_HasSolution() override;

    //! pde barrier (continuous monitoring, single knock-out / knock-in)
    bool PDE_IsBarrier() override;
    bool PDE_IsKnockIn() override;
    bool PDE_IsUpBarrier() override;
    bool PDE_IsDiscreteBarrier() override
    {
        return IsDiscrete();
    }
    double PDE_BarrierLevel() override;

    //! analytical
    bool ANA_HasSolution() override;
    void ANA_EvalPrice() override;

  private:
    //! closed-form (Reiner-Rubinstein) price of the barrier for a given spot,
    //! continuous monitoring, no rebate. r is the risk-free rate, b the
    //! cost-of-carry, v the volatility, t the year fraction to maturity and
    //! df = exp(-r t) the discount factor.
    double ANA_BarrierPrice( double Spot,
                             double r,
                             double b,
                             double v,
                             double t,
                             double df );

  public:
    //! fixing dates
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;
    set<date> GetAmericanExerciseDates() override;

    //! constructeur / destructeur
    Barrier( const string& ObjectName );
    ~Barrier() override;
};
