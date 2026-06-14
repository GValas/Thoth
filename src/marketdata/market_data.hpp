#pragma once
#include "object.hpp"

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
