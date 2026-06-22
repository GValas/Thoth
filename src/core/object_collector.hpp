#pragma once
#include "thoth.hpp"
#include "object.hpp"

//! object_collector.hpp — the single owner of all live domain objects.
//!
//! Type-indexed object registry.
//!
//! Owns every object in a single name-keyed map (names are globally unique).
//! Add<T>/Get<T> replace the former per-type Add*/Get* boilerplate: a lookup by
//! any base type is resolved at runtime via dynamic_cast, so an object created
//! as a derived type stays retrievable through any of its base classes.
class ObjectCollector
{

  private:
    //! owns every object, keyed by name
    map<string, std::unique_ptr<Object>> _object_map;

  public:
    //! register an object (ownership taken). A name must map to a single object
    //! of a single type: re-adding the same name with a different type is a
    //! configuration error and fails loudly rather than reinterpret-casting.
    template <class T>
    T* Add( std::unique_ptr<T> o )
    {
        auto& slot = _object_map[o->GetName()]; //!< inserts an empty slot on first sight
        if ( !slot )
        {
            //! first registration of this name: store o. The slot's static type is
            //! Object* but we just moved in a T, so static_cast back to T* is safe.
            slot = std::move( o ); //!< adopt ownership; type is exactly T
            return static_cast<T*>( slot.get() );
        }
        //! name already present: it must denote the same (or a compatible) type.
        //! dynamic_cast checks that at runtime; failure means two YAML entries reuse
        //! one name for different kinds, which we reject rather than silently alias.
        T* existing = dynamic_cast<T*>( slot.get() );
        if ( !existing )
        {
            ERR( "object '" + slot->GetName() + "' is already declared with a different type" );
        }
        return existing; //!< duplicate name of the same type -> reuse, drop o
    }

    //! look up by name as any (base) type T; nullptr if absent or not a T
    template <class T = Object>
    T* Get( const string& ObjectName ) const
    {
        auto it = _object_map.find( ObjectName );
        return it == _object_map.end() ? nullptr : dynamic_cast<T*>( it->second.get() );
    }
};
