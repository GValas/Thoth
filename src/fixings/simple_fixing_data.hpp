#pragma once
#include "object.hpp"

//! ----------------------------------------------------------------------------
//! SimpleFixingData : the historical (already-observed) fixings of one
//! underlying, as two parallel arrays (date[i] -> value[i]) plus the name of the
//! underlying they belong to. Path-dependent instruments (Asians, barriers,
//! variance swaps...) read these to pick up fixings that fall before the
//! valuation date, where there is nothing left to diffuse and the realised value
//! must be used instead of a simulated one.
//! ----------------------------------------------------------------------------
class SimpleFixingData : public Object
{

  private:
    vector<date> _date_list; //!< observation dates, parallel to _value_list
    LaVector _value_list;    //!< realised fixing levels (RAII linalg vector)
    string _underlying;      //!< name of the underlying these fixings belong to

  public:
    //! read own fields (dates + values + the underlying name) from the book
    void Configure( ObjectReader& reader ) override;

    //! getter
    const vector<date> GetDateList();
    la_vector* GetValueList(); //!< borrowed pointer (still owned by this object)
    string GetUnderlying();

    //! constructor, destructor
    SimpleFixingData( const string& ObjectName );
    ~SimpleFixingData() override;
};
