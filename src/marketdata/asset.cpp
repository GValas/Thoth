#include "thoth.hpp"
#include "asset.hpp"

//! constructor
Asset::Asset( const string& ObjectName,
              const string& ObjectKind ) : Object( ObjectName, ObjectKind )
{
}

//! destructor
Asset::~Asset() = default;

//! setter
void Asset::SetCurrency( Currency& Currency )
{
    _currency = &Currency;
}

//! setter
void Asset::SetToday( const date& Today )
{
    _currency->SetToday( Today );
    Object::SetToday( Today );
}

//! getter
Currency* Asset::GetCurrency() const
{
    return _currency;
}
