#pragma once
#include "contract.hpp"

//! A vanilla option: european or american call/put on one underlying. Priced by
//! ANA (european closed form), PDE (incl. american), MCL and the GPU GBM kernel.
class Vanilla : public Contract
{

  private:
    double _strike = 0;                                   //!< exercise strike K
    date _maturity_date;                                  //!< expiry / single settlement date
    ExerciseMode _exercise_mode = ExerciseMode::European; //!< European vs American
    OptionType _type = OptionType::Call;                  //!< Call vs Put

  public:
    //! read own fields from the configuration (strike / maturity / type / exercise)
    void Configure( ObjectReader& reader ) override;

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

    //! gpu monte-carlo (mcl_gpu) — only for European, single-asset, deterministic-vol GBM
    bool GPU_GbmParams( GpuGbmParams& Out ) override;

    //! fixing dates (single observation at maturity)
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;

    //! constructeur / destructeur
    Vanilla( const string& ObjectName );
    ~Vanilla() override;
};
