#pragma once
#include "currency.hpp"
#include "object.hpp"

//! An asset priced in a currency: the base of Single (a tradable spot) carrying the
//! pricing currency and propagating the valuation date down to it.
class Asset : public Object
{

  protected:
    //!
    Currency* _currency = nullptr;

  public:
    //! setter
    void SetCurrency( Currency& Currency );
    void SetToday( const date& Today ) override;

    //! getter
    Currency* GetCurrency() const;

    //! spot
    virtual double GetSpot() const = 0;

    //! constructor & destructor
    Asset( const string& ObjectName,
           const string& ObjectKind );
    ~Asset() override;
};
