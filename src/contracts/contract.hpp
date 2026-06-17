#pragma once
#include "underlying.hpp"
#include "valuation.hpp"
#include "pricing_facets.hpp"

//! A contract: its trade definition + native Monte-Carlo node graph, plus the
//! optional PDE / analytic / GPU pricing facets (see pricing_facets.hpp). The
//! engines write the priced result into Result().
class Contract : public Object, public PdePriceable, public AnaPriceable, public GpuPriceable
{

  protected:
    //! the priced result (premium + Greeks); engines write it via Result()
    Valuation _valuation;

    //! attributes
    Underlying* _underlying = nullptr;
    Currency* _premium_currency = nullptr;
    Correlation* _correlation = nullptr; //! for quanto purposes

    // mcl indexes
    vector<int> vect_idx_flow_date;   // flow dates
    vector<int> vect_idx_fixing_date; // fixing dates
    int idx_underlying = 0;           // underling position
    int idx_contract = 0;             // contract position

  public:
    //! setter
    void SetUnderlying( Underlying& underlying );
    void SetPremiumCurrency( Currency& premium_currency );
    void SetToday( const date& Today ) override;
    void SetCorrelation( Correlation* Correlation );

    //! getter
    Underlying* GetUnderlying();
    Currency* GetPremiumCurrency();

    SingleSet GetSingleSet();

    //! the priced result (premium + Greeks): engines fill it, aggregation reads it
    Valuation& Result() { return _valuation; }
    const Valuation& Result() const { return _valuation; }

    //! mcl nodes (the contract's canonical definition)
    MonteCarloNode* GetNode( NodeCollector& NC );
    MonteCarloNode* GetUnderlyingNode( NodeCollector& NC );
    virtual MonteCarloNode* GetFlowNode( NodeCollector& NC, const date& AsOfDate ) = 0;

    virtual date GetMaturityDate() = 0;

    //! trade properties shared across engines (PDE boundary + MCL American LSM):
    //! the intrinsic (exercise) payoff at a given spot, and the exercise style.
    virtual double Intrinsic( const double spot ) = 0;
    virtual bool IsAmerican() = 0;

    //! fixing dates
    virtual set<date> GetFixingDates() = 0;
    virtual set<date> GetFlowDates() = 0;
    virtual set<date> GetAmericanExerciseDates() = 0;

    //! PDE / analytic / GPU pricing facets are inherited from pricing_facets.hpp:
    //!   PdePriceable : PDE_HasSolution + barrier / accrued-variance flags
    //!   AnaPriceable : ANA_HasSolution / ANA_EvalPrice
    //!   GpuPriceable : GPU_GbmParams

    //! constructor
    Contract( const string& ObjectName,
              const string& ObjectKind );
    ~Contract() override;
};
