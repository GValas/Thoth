#pragma once
#include "object.hpp"

//! Base class for a market-data object (curve, volatility, ...): a named Object the
//! engines read; concrete kinds add the actual data and accessors.
class MarketData : public Object

{

  protected:
    //! attribute
    // date _value_date;

  public:
    //! setter
    // void SetValueDate( date ValueDate );

    MarketData( const string& ObjectName,
                const string& ObjectKind );
    ~MarketData() override;
};
