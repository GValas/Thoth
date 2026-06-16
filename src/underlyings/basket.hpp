#pragma once
#include "underlying.hpp"

//! Abstract multi-asset basket of underlyings (base of AbsoluteBasket): holds the
//! component list and aggregates their single-name / currency sets.
class Basket : public Underlying
{

  protected:
    vector<Underlying*> _underlying_list;

  public:
    //! setter
    void SetUnderlyingList( const vector<Underlying*>& UnderlyingList );
    void SetToday( const date& Today ) override;
    void SetCorrelation( Correlation* Correlation ) override;

    //!
    vector<Underlying*> GetUnderlyingList();
    SingleSet GetSingleSet() override;
    CurrencySet GetCurrencySet() override;

    //! constructor, destructor
    Basket( const string& ObjectName,
            const string& ObjectKind );
    ~Basket() override;
};
