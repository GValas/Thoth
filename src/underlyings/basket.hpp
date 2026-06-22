#pragma once
#include "underlying.hpp"

//! basket.hpp — common machinery for multi-asset baskets.
//!
//! Basket is the abstract base shared by AbsoluteBasket (weighted-sum basket) and
//! Rainbow (best/worst-of). It owns the component list, the fixed rebasing
//! reference spots, and the fan-out behaviour (propagating Today / Correlation to
//! every member and unioning their single-name / currency sets). The actual
//! forward / vol / node construction is left to the concrete subclasses.

//! Abstract multi-asset basket of underlyings (base of AbsoluteBasket): holds the
//! component list and aggregates their single-name / currency sets.
class Basket : public Underlying
{

  protected:
    //! the basket components (non-owning pointers; lifetime managed by the object
    //! registry). Order is significant: it indexes _ref_spots and the weight vector.
    vector<Underlying*> _underlying_list;

    //! trade-inception component spots, used as the FIXED rebasing reference
    //! (S_i0) in the derived baskets. Captured once at load (CaptureReferenceSpots)
    //! so a delta/gamma spot bump scales S_i against a constant S_i0 instead of
    //! cancelling — otherwise rebasing by the live, bumped spot zeroes the Greek.
    vector<double> _ref_spots;

  public:
    //! snapshot the components' current spots as the rebasing reference. Called at
    //! load, before any pricing, so the reference is the inception spot.
    void CaptureReferenceSpots();

    //! the captured reference spot of component i (falls back to the live spot if
    //! capture has not run, so behaviour is unchanged when unused).
    double RefSpot( size_t i ) const;

    //! setter — install the ordered component list (call before CaptureReferenceSpots).
    void SetUnderlyingList( const vector<Underlying*>& UnderlyingList );
    //! set the valuation date; overridden to fan the date out to every member first,
    //! then set it on this basket (so component forwards reprice off the same Today).
    void SetToday( const date& Today ) override;
    //! inject the correlation matrix; overridden to propagate it to every member
    //! before recording it here (members need it for their own quanto/FX queries).
    void SetCorrelation( Correlation* Correlation ) override;

    //! the ordered component list.
    vector<Underlying*> GetUnderlyingList() const;
    //! union of the components' single-name sets (the basket's full factor universe).
    SingleSet GetSingleSet() const override;
    //! union of the components' currency sets plus the basket's own currency.
    CurrencySet GetCurrencySet() const override;

    //! a (weighted) basket collapses to one effective asset via moment matching,
    //! so the 1-D grid / closed form apply. Rainbow overrides this back to false:
    //! its best/worst-of payoff orders the components and needs their joint law.
    bool IsGriddable() const override { return true; }

    //! constructor, destructor — forward name + concrete kind tag to Underlying.
    Basket( const string& ObjectName,
            const string& ObjectKind );
    ~Basket() override;
};
