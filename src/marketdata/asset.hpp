#pragma once
#include "currency.hpp"
#include "object.hpp"

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
    Currency* GetCurrency();

    //! spot
    virtual double GetSpot() = 0;

    //! constructor & destructor
    Asset( const string& ObjectName,
           const string& ObjectKind );
    ~Asset() override;
};
