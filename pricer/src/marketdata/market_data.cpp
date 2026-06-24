#include "market_data.hpp"

//! market_data.cpp — MarketData implementation. The ApplyShift/HasFactor defaults are
//! inline in the header; only the trivial ctor/dtor live here. Concrete kinds (Curve,
//! Volatility, ...) override the bump contract.

//! constructor — forwards the name/kind to Object; no own state to initialise
MarketData::MarketData( const string& ObjectName,
                        const string& ObjectKind ) : Object( ObjectName, ObjectKind )
{
}

//! destructor
MarketData::~MarketData() = default;

/*
//! setter
void MarketData::SetValueDate( date ValueDate )
{
    _value_date = ValueDate;
}
*/