#pragma once
#include "object.hpp"

class SimpleFixingData : public Object
{

  private:
    vector<date> _date_list;
    GslVector _value_list;
    string _underlying;

  public:
    //! setter
    void SetDateList( const vector<date>& DateList );
    void SetValueList( gsl_vector* ValueList );
    void SetUnderlying( const string& Underlying );

    //! getter
    const vector<date> GetDateList();
    gsl_vector* GetValueList();
    string GetUnderlying();

    //! constructor, destructor
    SimpleFixingData( const string& ObjectName );
    ~SimpleFixingData() override;
};
