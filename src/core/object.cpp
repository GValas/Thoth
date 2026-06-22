//! object.cpp — out-of-line definitions for the Object base class.
//! Only the trivial identity accessors and the date setter live here; everything
//! else (Configure, the derived behaviour) is provided by the concrete types.
#include "thoth.hpp"
#include "object.hpp"

//! constructor — store the immutable identity. _today is left default-constructed
//! and filled later by SetToday once the valuation date is known.
Object::Object( const string& ObjectName,
                const string& ObjectKind ) : _name( ObjectName ), _kind( ObjectKind )
{
}

//! destructor — defaulted but defined out-of-line so the vtable / key function is
//! anchored in this translation unit.
Object::~Object() = default;

//! getter — the unique object name (also used as the YAML path prefix)
const string& Object::GetName() const
{
    return _name;
}

//! getter — the kind tag (one of the KIND_* constants)
const string& Object::GetKind() const
{
    return _kind;
}

//! setter — base implementation just records the valuation date; derived types
//! that cache date-dependent data override this and recompute after calling here.
void Object::SetToday( const date& Today )
{
    _today = Today;
}