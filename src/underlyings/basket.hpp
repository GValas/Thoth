#pragma once
#include "underlying.hpp"

//! Abstract multi-asset basket of underlyings (base of AbsoluteBasket): holds the
//! component list and aggregates their single-name / currency sets.
class Basket : public Underlying
{

  protected:
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

    //! setter
    void SetUnderlyingList( const vector<Underlying*>& UnderlyingList );
    void SetToday( const date& Today ) override;
    void SetCorrelation( Correlation* Correlation ) override;

    //!
    vector<Underlying*> GetUnderlyingList() const;
    SingleSet GetSingleSet() const override;
    CurrencySet GetCurrencySet() const override;

    //! a (weighted) basket collapses to one effective asset via moment matching,
    //! so the 1-D grid / closed form apply. Rainbow overrides this back to false:
    //! its best/worst-of payoff orders the components and needs their joint law.
    bool IsGriddable() const override { return true; }

    //! constructor, destructor
    Basket( const string& ObjectName,
            const string& ObjectKind );
    ~Basket() override;
};
