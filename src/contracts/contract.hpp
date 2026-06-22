#pragma once
#include "underlying.hpp"
#include "valuation.hpp"
#include "pricing_facets.hpp"

//! Abstract base for every priced instrument (vanilla, barrier, variance swap...).
//!
//! A Contract bundles three things:
//!   1. the *trade definition* — its underlying, premium (settlement) currency and
//!      payoff/exercise schedule, read from YAML in the concrete Configure();
//!   2. the *canonical Monte-Carlo node graph* — GetNode() builds a ContractNode
//!      from per-flow-date sub-trees (GetFlowNode), shared/deduplicated through the
//!      NodeCollector so a whole book reuses common spot/vol/rate nodes;
//!   3. the *optional pricing facets* (PdePriceable / AnaPriceable / GpuPriceable,
//!      see pricing_facets.hpp) — alternative routes a given engine may take when
//!      the underlying supports them.
//! The pricing engines write the premium + Greeks into Result() (a Valuation); book
//! aggregation and the output reader read it back. Concrete contracts supply the
//! payoff (Intrinsic / GetFlowNode), the schedule (the Get*Dates getters) and the
//! facet predicates; the common plumbing lives here.
class Contract : public Object, public PdePriceable, public AnaPriceable, public GpuPriceable
{

  protected:
    //! the priced result (premium + Greeks); engines write it via Result()
    Valuation _valuation;

    //! attributes
    Underlying* _underlying = nullptr;     //!< what the payoff references (single / basket / rainbow)
    Currency* _premium_currency = nullptr; //!< settlement currency; discounting & forward measure
    Correlation* _correlation = nullptr;   //! for quanto purposes (spot/FX correlation lookup)

    // mcl indexes (positions into the engine's flat date / underlying / contract tables)
    vector<int> _vect_idx_flow_date;   // flow dates
    vector<int> _vect_idx_fixing_date; // fixing dates
    int _idx_underlying = 0;           // underling position
    int _idx_contract = 0;             // contract position

    //! read the attributes common to every contract (underlying + premium
    //! currency, with the basket/rainbow currency override). Called by each
    //! concrete contract's Configure after it has read its own fields.
    void ConfigureCommon( ObjectReader& reader );

  public:
    //! setter — bind the referenced underlying (non-owning)
    void SetUnderlying( Underlying& underlying );
    //! setter — bind the settlement / discounting currency (non-owning)
    void SetPremiumCurrency( Currency& premium_currency );
    //! propagate the valuation date down to the currency and underlying, then base
    void SetToday( const date& Today ) override;
    //! setter — supply the correlation object used to build the quanto adjustment
    void SetCorrelation( Correlation* Correlation );

    //! getter
    [[nodiscard]] Underlying* GetUnderlying() const;
    [[nodiscard]] Currency* GetPremiumCurrency() const;

    //! the set of single names this contract's underlying decomposes into
    SingleSet GetSingleSet() const;

    //! the priced result (premium + Greeks): engines fill it, aggregation reads it
    Valuation& Result() { return _valuation; }
    const Valuation& Result() const { return _valuation; }

    //! mcl nodes (the contract's canonical definition)
    //! the contract node: a discounted sum-product over its per-date flow nodes
    MonteCarloNode* GetNode( NodeCollector& NC );
    //! the (possibly quanto-adjusted) spot node the payoff observes
    MonteCarloNode* GetUnderlyingNode( NodeCollector& NC );
    //! concrete payoff sub-tree settling on AsOfDate (one per flow date)
    virtual MonteCarloNode* GetFlowNode( NodeCollector& NC, const date& AsOfDate ) = 0;

    virtual date GetMaturityDate() const = 0;

    //! trade properties shared across engines (PDE boundary + MCL American LSM):
    //! the intrinsic (exercise) payoff at a given spot, and the exercise style.
    virtual double Intrinsic( const double spot ) = 0;
    virtual bool IsAmerican() = 0;

    //! fixing dates: spot observations the diffusion must produce
    virtual set<date> GetFixingDates() = 0;
    //! flow dates: dates on which a cash flow settles (drive GetNode's sub-trees)
    virtual set<date> GetFlowDates() = 0;
    //! candidate early-exercise dates (American LSM); maturity-only for European
    virtual set<date> GetAmericanExerciseDates() = 0;

    //! PDE / analytic / GPU pricing facets are inherited from pricing_facets.hpp:
    //!   PdePriceable : PDE_HasSolution + barrier / accrued-variance flags
    //!   AnaPriceable : ANA_HasSolution / ANA_EvalPrice
    //!   GpuPriceable : GPU_GbmParams

    //! constructor — ObjectName is the trade id, ObjectKind the contract KIND tag
    Contract( const string& ObjectName,
              const string& ObjectKind );
    ~Contract() override;
};
