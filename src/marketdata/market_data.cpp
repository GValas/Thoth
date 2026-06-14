#include "market_data.hpp"

//!
MarketData::MarketData( const string& ObjectName,
                        const string& ObjectKind ) : Object( ObjectName, ObjectKind )
{
}

//!
MarketData::~MarketData() = default;

/*
//! setter
void MarketData::SetValueDate( date ValueDate )
{
    _value_date = ValueDate;
}
*/