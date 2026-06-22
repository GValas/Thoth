#pragma once
#include "object_manager.hpp"

//! object_reader.hpp — the typed field-reading facade handed to Object::Configure.
//! It turns "this object reads itself from YAML" into a handful of type-dispatched
//! templates (Get/Has/Ref) over its own node, decoupling deserialisation from both
//! the ObjectManager and the registry.

//! trait: is T a vector<...>? Lets Ref<T> fold the scalar and list reference reads
//! into one template (a member variable template cannot be partially specialised, so
//! this lives at namespace scope).
namespace reader_detail
{
template <class>
inline constexpr bool is_vector = false;
template <class U>
inline constexpr bool is_vector<vector<U>> = true;
} // namespace reader_detail

//! A typed field reader bound to one object's YAML node.
//!
//! It isolates "how an object reads itself from the configuration" from the
//! ObjectManager / registry: a concrete object's Configure(ObjectReader&) reads
//! its own fields and resolves its own references through this thin facade,
//! symmetrically to how it builds its own Monte-Carlo nodes (GetFlowNode).
//!
//! It is a concrete class (not a pure interface) on purpose: reference resolution is
//! type-dependent — the same `equity` is reached as an Equity, a Single or an
//! Underlying depending on the referencing field's declared target — so it must go
//! through the templated ObjectManager::Get<T> (a dynamic_cast), which a type-erased
//! virtual interface could not express correctly.
class ObjectReader
{
    ObjectManager& _m; //!< the manager, for field access (yml()) and reference resolution
    string _n;         //!< the object's name (dotted-path prefix)

    //! qualify a bare field name into the object's full YAML path ("<name>.<field>")
    string Path( const string& Field ) const { return _n + OBJECT_SEPARATOR + Field; }

  public:
    //! bind the reader to one object (by name) and the manager that holds the config
    ObjectReader( ObjectManager& Manager, string ObjectName )
        : _m( Manager ), _n( std::move( ObjectName ) )
    {
    }

    //! --- field read, dispatched on the requested type by YamlConfig::Get<T>. T is a
    //! scalar (Get<double>("strike"), Get<date>("maturity"), ...) or a list
    //! (Get<vector<double>>("weights"), ...). The type -> parse mapping lives once, in
    //! YamlConfig; here we only qualify the bare field name into the object's path. ---
    template <class T>
    T Get( const string& f ) { return _m.yml().Get<T>( Path( f ) ); }

    //! --- scalar (optional, with a default) : Get<double>("notional", 1) ---
    template <class T>
    T Get( const string& f, const T& d ) { return _m.yml().Get<T>( Path( f ), d ); }

    //! the la_vector view is a raw pointer (not a vector<T>), so it stays named —
    //! a zero-copy view onto the YAML-backed buffer rather than a value type.
    la_vector* LaVector( const string& f ) { return _m.yml().GetLaVector( Path( f ) ); }

    //! --- presence of an optional field, scalar (Has<string>("calendar")) or list
    //! (Has<vector<double>>("matrix")) ---
    template <class T>
    bool Has( const string& f ) { return _m.yml().Has<T>( Path( f ) ); }

    //! --- typed references: the field holds the *name(s)* of other object(s); each
    //! is got-or-built as the target type. Scalar (Ref<Currency>("rate") ->
    //! Currency*) or list (Ref<vector<Forex>>("forexs") -> vector<Forex*>).
    //! Resolution is type-dependent (see the class comment), hence the target type. ---
    template <class T>
    auto Ref( const string& f )
    {
        if constexpr ( reader_detail::is_vector<T> )
        {
            //! list reference: read the field as a list of names, then get-or-build
            //! each as the element type, yielding a vector of pointers (vector<E*>).
            using E = typename T::value_type; //!< T = vector<E> -> vector<E*>
            return _m.GetList<E>( _m.yml().GetStringList( Path( f ) ) );
        }
        else
        {
            //! scalar reference: the field holds one name; get-or-build it as a T*.
            return _m.Get<T>( _m.yml().GetString( Path( f ) ) );
        }
    }

    //! --- escape hatch for the few irreducible cases (e.g. a factory that picks a
    //! concrete subclass before configuring) ---
    ObjectManager& Manager() { return _m; }   //!< raw access when the facade is too narrow
    const string& Name() const { return _n; } //!< the bound object's name / path prefix
};
