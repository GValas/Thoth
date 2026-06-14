#include "thoth.hpp"
#include "object.hpp"

//! constructor
Object::Object( const string& ObjectName,
                const string& ObjectKind ) : _name( ObjectName ), _kind( ObjectKind )
{
}

//! destructor
Object::~Object() = default;

//! getter
const string& Object::GetName() const
{
    return _name;
}

//! getter
const string& Object::GetKind() const
{
    return _kind;
}

//! setter
void Object::SetToday( const date& Today )
{
    _today = Today;
}