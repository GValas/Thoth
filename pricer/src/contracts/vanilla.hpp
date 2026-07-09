#pragma once
#include "contract.hpp"

//! A vanilla option: european or american call/put on one underlying. Priced by
//! ANA (european closed form), PDE (incl. american), MCL and the GPU GBM kernel.
class Vanilla : public Contract
{

  private:
    double _strike = 0;                                   //!< resolved cash strike K (see SetToday)
    double _strike_input = 0;                             //!< configured strike (cash, or % of spot)
    bool _is_absolute_strike = true;                      //!< false: strike is a percent of the spot
    date _maturity_date;                                  //!< expiry / single settlement date
    ExerciseMode _exercise_mode = ExerciseMode::European; //!< European vs American
    OptionType _type = OptionType::Call;                  //!< Call vs Put

  public:
    //! read own fields from the configuration (strike / maturity / type / exercise)
    void Configure( ObjectReader& reader ) override;

    //! roll the valuation date, then resolve a relative strike against the base
    //! (unbumped) spot so the cash strike stays fixed through Greek bumps
    void SetToday( const date& Today ) override;

    //! getter
    [[nodiscard]] double GetStrike() const;
    [[nodiscard]] OptionType GetType() const;
    date GetMaturityDate() const override;

    //! mcl node — a VanillaFlowNode (call/put payoff at maturity)
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! trade properties (intrinsic payoff + exercise style)
    double Intrinsic( const double spot ) override;
    bool IsAmerican() override;

    //! fixing dates (single observation at maturity)
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;

    //! constructeur / destructeur
    Vanilla( const string& ObjectName );
    ~Vanilla() override;
};
