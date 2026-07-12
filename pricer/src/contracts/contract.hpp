#pragma once
#include "underlying.hpp"
#include "valuation.hpp"

//! Abstract base for every priced instrument (vanilla, barrier, variance swap...).
//!
//! A Contract bundles two things:
//!   1. the *trade definition* — its underlying, premium (settlement) currency and
//!      payoff/exercise schedule, read from YAML in the concrete Configure();
//!   2. the *canonical Monte-Carlo node graph* — GetNode() builds a ContractNode
//!      from per-flow-date sub-trees (GetFlowNode), shared/deduplicated through the
//!      NodeCollector so a whole book reuses common spot/vol/rate nodes.
//! A contract is a pure *description*: it holds no priced result, and it does not
//! decide which engine can price it — whether a PDE / closed form / GPU kernel
//! applies is an engine decision (PricerPDE / PricerANA / PricerMCL inspect the
//! contract + underlying, down-casting to the concrete type for any per-flavour
//! detail, e.g. a Barrier's level). The pricing engines (tasks) keep the premium +
//! Greeks themselves (Pricer::Result). Concrete contracts supply the payoff
//! (Intrinsic / GetFlowNode) and the schedule (the Get*Dates getters); the common
//! plumbing lives here.
class Contract : public Object
{

  protected:
    //! attributes
    Underlying* _underlying = nullptr;     //!< what the payoff references (single / basket / rainbow)
    Currency* _premium_currency = nullptr; //!< settlement currency; discounting & forward measure
    Correlation* _correlation = nullptr;   //! for quanto purposes (spot/FX correlation lookup)

    // mcl indexes (positions into the engine's flat date / underlying / contract tables)
    vector<int> _vect_idx_flow_date;   // flow dates
    vector<int> _vect_idx_fixing_date; // fixing dates
    int _idx_underlying = 0;           // underling position
    int _idx_contract = 0;             // contract position

  public:
    //! read the attributes common to every contract (underlying + premium currency,
    //! with the basket/rainbow currency override). Each concrete contract's Configure
    //! calls this base first, then reads its own fields.
    void Configure( ObjectReader& reader ) override;

    //! propagate the valuation date down to the currency and underlying, then base
    void SetToday( const date& Today ) override;
    //! setter — supply the correlation object used to build the quanto adjustment
    void SetCorrelation( Correlation* Correlation );

    //! getter
    [[nodiscard]] Underlying* GetUnderlying() const;
    [[nodiscard]] Currency* GetPremiumCurrency() const;

    //! the set of single names this contract's underlying decomposes into
    SingleSet GetSingleSet() const;

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

    //! the LSM regression's moneyness normaliser: the level the exercise boundary
    //! sits near, so the regressors stay O(1) there. Default: the path's initial
    //! spot; a Vanilla overrides with its strike. PathInitialSpot is the recorded
    //! base-path spot at today.
    virtual double LsmBasisNorm( double PathInitialSpot ) { return PathInitialSpot; }

    //! fixing dates: spot observations the diffusion must produce
    virtual set<date> GetFixingDates() = 0;
    //! flow dates: dates on which a cash flow settles (drive GetNode's sub-trees)
    virtual set<date> GetFlowDates() = 0;

    //! constructor — ObjectName is the trade id, ObjectKind the contract KIND tag
    Contract( const string& ObjectName,
              const string& ObjectKind );
    ~Contract() override;
};
