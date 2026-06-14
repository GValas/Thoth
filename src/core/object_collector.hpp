#pragma once
#include "thoth.hpp"
#include "object.hpp"

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

    //! owns auxiliary objects that intentionally share a name with another
    //! object (e.g. a mono wrapper around an equity) and so cannot be indexed
    vector<std::unique_ptr<Object>> _owned_extra;

  public:
    //! register an object (ownership taken). A name must map to a single object
    //! of a single type: re-adding the same name with a different type is a
    //! configuration error and fails loudly rather than reinterpret-casting.
    template <class T>
    T* Add( std::unique_ptr<T> o )
    {
        auto& slot = _object_map[o->GetName()];
        if ( !slot )
        {
            slot = std::move( o ); //!< adopt ownership; type is exactly T
            return static_cast<T*>( slot.get() );
        }
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

    //! take ownership of an auxiliary object not exposed for lookup
    template <class T>
    T* Own( std::unique_ptr<T> o )
    {
        T* p = o.get();
        _owned_extra.push_back( std::move( o ) );
        return p;
    }

    //! propagate the valuation date to every object
    void SetToday( const date& Today )
    {
        for ( auto& o : _object_map )
        {
            o.second->SetToday( Today );
        }
        for ( auto& o : _owned_extra )
        {
            o->SetToday( Today );
        }
    }
};
