#include "thoth.hpp"
#include "simple_fixing_data.hpp"
#include "object_reader.hpp"

SimpleFixingData::SimpleFixingData( const string& ObjectName ) : Object( ObjectName, KIND_SIMPLE_FIXING_DATA )
{
}

SimpleFixingData::~SimpleFixingData() = default;

//! read the fixing time series (dates + values) and its underlying name
void SimpleFixingData::Configure( ObjectReader& reader )
{
    SetDateList( reader.Get<vector<date>>( "dates" ) );
    SetValueList( reader.LaVector( "values" ) );
    SetUnderlying( reader.Get<string>( "underlying" ) );
}

//! setter
void SimpleFixingData::SetDateList( const vector<date>& DateList )
{
    _date_list = DateList;
}

//! setter
void SimpleFixingData::SetValueList( la_vector* ValueList )
{
    _value_list = ValueList;
}

//! setter
void SimpleFixingData::SetUnderlying( const string& Underlying )
{
    _underlying = Underlying;
}

//! getter
const vector<date> SimpleFixingData::GetDateList()
{
    return _date_list;
}

//! getter
la_vector* SimpleFixingData::GetValueList()
{
    return _value_list;
}

//! getter
string SimpleFixingData::GetUnderlying()
{
    return _underlying;
}
