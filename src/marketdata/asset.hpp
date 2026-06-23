#pragma once
#include "currency.hpp"
#include "object.hpp"

//! asset.hpp — the priced-in-a-currency abstraction.
//!
//! An asset priced in a currency: the base of Single (a tradable spot) carrying the
//! pricing currency and propagating the valuation date down to it. It owns no spot or
//! vol of its own (those live on Single); its sole responsibility is the link to the
//! pricing Currency and the date-propagation that keeps that currency's curve aligned
//! with the book's valuation date.
class Asset : public Object
{

  protected:
    //! the currency this asset is priced/quoted in (non-owning; the Currency is a
    //! shared singleton in the book graph). Carries the discount/yield curve used for
    //! forwards, discounting and dividend escrow. Set during Configure of the
    //! concrete name (Equity / Forex).
    Currency* _currency = nullptr;

  public:
    //! setter — bind the pricing currency (stored by address, not owned)
    void SetCurrency( Currency* Currency );
    //! propagate the valuation date down into the pricing currency (and its curve)
    //! before delegating to the base Object, so every date-dependent input is aligned
    void SetToday( const date& Today ) override;

    //! getter — the pricing currency (and through it, the discount/yield curve)
    Currency* GetCurrency() const;

    //! spot — the current price of the asset; supplied by the concrete name
    virtual double GetSpot() const = 0;

    //! constructor & destructor
    Asset( const string& ObjectName,
           const string& ObjectKind );
    ~Asset() override;
};
