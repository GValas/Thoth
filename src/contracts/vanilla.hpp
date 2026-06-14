#pragma once
#include "contract.hpp"

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
    double GetStrike();
    date GetMaturityDate() override;

    //! mcl node
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! pde
    bool PDE_HasSolution() override;
    double PDE_EvalFlow( const double spot ) override;
    bool PDE_IsAmerican() override;

    //! analytical
    bool ANA_HasSolution() override;
    void ANA_EvalPrice() override;

    //! fixing dates
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;
    set<date> GetAmericanExerciseDates() override;

    //! constructeur / destructeur
    Vanilla( const string& ObjectName );
    ~Vanilla() override;
};
