#pragma once
#include "contract.hpp"

//! A vanilla option: european or american call/put on one underlying. Priced by
//! ANA (european closed form), PDE (incl. american), MCL and the GPU GBM kernel.
class Vanilla : public Contract
{

  private:
    double _strike = 0;
    date _maturity_date;
    ExerciseMode _exercise_mode = ExerciseMode::European;
    OptionType _type = OptionType::Call;

  public:
    //! setter
    void SetStrike( const double Strike );
    void SetMaturityDate( const date& MaturityDate );
    void SetExerciseMode( ExerciseMode Mode );
    void SetType( OptionType Type );

    //! getter
    [[nodiscard]] double GetStrike() const;
    date GetMaturityDate() override;

    //! mcl node
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! trade properties (intrinsic payoff + exercise style)
    double Intrinsic( const double spot ) override;
    bool IsAmerican() override;

    //! pde
    bool PDE_HasSolution() override;

    //! analytical
    bool ANA_HasSolution() override;
    void ANA_EvalPrice() override;

    //! gpu monte-carlo (mcl_gpu)
    bool GPU_GbmParams( GpuGbmParams& Out ) override;

    //! fixing dates
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;
    set<date> GetAmericanExerciseDates() override;

    //! constructeur / destructeur
    Vanilla( const string& ObjectName );
    ~Vanilla() override;
};
