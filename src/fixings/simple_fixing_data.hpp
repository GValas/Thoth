#pragma once
#include "object.hpp"

//! A simple time series of past fixings (dates + values) for one underlying.
class SimpleFixingData : public Object
{

  private:
    vector<date> _date_list;
    LaVector _value_list;
    string _underlying;

  public:
    //! setter
    void SetDateList( const vector<date>& DateList );
    void SetValueList( la_vector* ValueList );
    void SetUnderlying( const string& Underlying );

    //! getter
    const vector<date> GetDateList();
    la_vector* GetValueList();
    string GetUnderlying();

    //! constructor, destructor
    SimpleFixingData( const string& ObjectName );
    ~SimpleFixingData() override;
};
