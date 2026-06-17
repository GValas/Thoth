#include "thoth.hpp"
#include "basket.hpp"

//!
Basket::Basket( const string& ObjectName,
                const string& ObjectKind ) : Underlying( ObjectName, ObjectKind )
{
}

//!
Basket::~Basket() = default;

//! setter
void Basket::SetUnderlyingList( const vector<Underlying*>& UnderlyingList )
{
    _underlying_list = UnderlyingList;
}

//! snapshot the components' spots as the fixed rebasing reference (S_i0)
void Basket::CaptureReferenceSpots()
{
    _ref_spots.clear();
    for ( Underlying* u : _underlying_list )
    {
        _ref_spots.push_back( u->GetSpot() );
    }
}

//! captured reference spot of component i, or its live spot if not captured
double Basket::RefSpot( size_t i )
{
    return ( i < _ref_spots.size() ) ? _ref_spots[i] : _underlying_list[i]->GetSpot();
}

//! setter
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

//! setter
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

//! list of singles
SingleSet Basket::GetSingleSet()
{
    SingleSet s;
    vector<Underlying*>::iterator u;
    for ( u = _underlying_list.begin();
          u != _underlying_list.end();
          u++ )
    {
        SingleSet s_ = ( *u )->GetSingleSet();
        s.insert( s_.begin(), s_.end() );
    }
    return s;
}

//! list of singles
CurrencySet Basket::GetCurrencySet()
{
    CurrencySet s;
    vector<Underlying*>::iterator u;
    for ( u = _underlying_list.begin();
          u != _underlying_list.end();
          u++ )
    {
        CurrencySet s_ = ( *u )->GetCurrencySet();
        s.insert( s_.begin(), s_.end() );
    }
    s.insert( _currency );
    return s;
}
