#include "thoth.hpp"
#include "asset.hpp"

//! asset.cpp — Asset implementation: currency binding + date propagation.

//! constructor — just forwards the name/kind to Object; the currency is wired later
//! during Configure of the concrete name.
Asset::Asset( const string& ObjectName,
              const string& ObjectKind ) : Object( ObjectName, ObjectKind )
{
}

//! destructor
Asset::~Asset() = default;

//! setter — bind the pricing currency by address (Currency is a shared singleton, so
//! we keep a non-owning pointer)
void Asset::SetCurrency( Currency* Currency )
{
    _currency = Currency;
}

//! setter — push the valuation date into the pricing currency first (so its discount
//! curve is dated before any forward/discount call), then set it on this Object
void Asset::SetToday( const date& Today )
{
    _currency->SetToday( Today );
    Object::SetToday( Today );
}

//! getter — the bound pricing currency
Currency* Asset::GetCurrency() const
{
    return _currency;
}
