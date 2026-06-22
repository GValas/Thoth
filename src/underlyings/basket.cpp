#include "thoth.hpp"
#include "basket.hpp"
#include "correlation.hpp" //!< propagate correlation to members
#include "single.hpp"      //!< complete Single for the aggregated SingleSet

//! basket.cpp — Basket base-class behaviour: component storage, the fixed rebasing
//! reference, and the fan-out of Today/Correlation plus the set aggregation shared
//! by every basket shape. No forward/vol/node logic here (subclass responsibility).

//! ctor — nothing to initialise beyond the base; subclass sets the component list.
Basket::Basket( const string& ObjectName,
                const string& ObjectKind ) : Underlying( ObjectName, ObjectKind )
{
}

//! components are non-owning, so default destruction suffices.
Basket::~Basket() = default;

//! setter — store the ordered component list verbatim (rebasing reference is
//! captured separately, after this, via CaptureReferenceSpots).
void Basket::SetUnderlyingList( const vector<Underlying*>& UnderlyingList )
{
    _underlying_list = UnderlyingList;
}

//! snapshot the components' spots as the fixed rebasing reference (S_i0).
//! Must run once at load, before any pricing/bumping: freezing S_i0 means a later
//! delta/gamma bump moves S_i against a constant denominator (S_i / S_i0 actually
//! changes) instead of moving numerator and denominator together (which would
//! pin every performance to 1 and zero the Greek).
void Basket::CaptureReferenceSpots()
{
    _ref_spots.clear();
    for ( Underlying* u : _underlying_list )
    {
        _ref_spots.push_back( u->GetSpot() );
    }
}

//! captured reference spot of component i, or its live spot if not captured.
//! The fallback keeps behaviour identical for any path that never calls
//! CaptureReferenceSpots (rebasing by the live spot is then a no-op factor).
double Basket::RefSpot( size_t i ) const
{
    return ( i < _ref_spots.size() ) ? _ref_spots[i] : _underlying_list[i]->GetSpot();
}

//! setter — fan the valuation date out to every component first, so their curves /
//! forwards are repriced off the new date, then record it on the basket itself.
void Basket::SetToday( const date& Today )
{
    vector<Underlying*>::iterator u;
    for ( u = _underlying_list.begin();
          u != _underlying_list.end();
          u++ )
    {
        ( *u )->SetToday( Today );
    }
    Underlying::SetToday( Today );
}

//! setter — push the shared correlation matrix down to every component (each member
//! needs it for its own quanto/FX queries), then store it on the basket via the base.
void Basket::SetCorrelation( Correlation* Correlation )
{
    vector<Underlying*>::iterator u;
    for ( u = _underlying_list.begin();
          u != _underlying_list.end();
          u++ )
    {
        ( *u )->SetCorrelation( Correlation );
    }
    Underlying::SetCorrelation( Correlation );
}

//! union of all components' single-name sets — the basket's full diffusion universe.
//! Using a set de-duplicates names shared across components (e.g. an overlap leg).
SingleSet Basket::GetSingleSet() const
{
    SingleSet s;
    for ( Underlying* u : _underlying_list )
    {
        SingleSet s_ = u->GetSingleSet();
        s.insert( s_.begin(), s_.end() );
    }
    return s;
}

//! union of all components' currency sets plus the basket's own settlement currency,
//! so the engine pulls every rate/FX curve the basket and its members discount on.
CurrencySet Basket::GetCurrencySet() const
{
    CurrencySet s;
    for ( Underlying* u : _underlying_list )
    {
        CurrencySet s_ = u->GetCurrencySet();
        s.insert( s_.begin(), s_.end() );
    }
    s.insert( _currency ); //!< basket's own currency, not necessarily among members'
    return s;
}
