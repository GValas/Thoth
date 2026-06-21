#pragma once
#include "object_manager.hpp"
#include <type_traits>

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
    ObjectManager& _m;
    string _n; //!< the object's name (dotted-path prefix)

    string Path( const string& Field ) const { return _n + OBJECT_SEPARATOR + Field; }

    //! the four field categories below dispatch on the requested C++ type instead of
    //! exposing one named accessor per type. Each helper maps T to the matching
    //! YamlConfig call at compile time (if constexpr), so adding a scalar/list type
    //! is a single branch rather than a new pair of methods. A T with no branch fails
    //! to compile (the static_assert below), keeping the mapping total and explicit.
    template <class>
    static constexpr bool unsupported = false;

  public:
    ObjectReader( ObjectManager& Manager, string ObjectName )
        : _m( Manager ), _n( std::move( ObjectName ) )
    {
    }

    //! --- field read, dispatched on the requested type. T is a scalar
    //! (Get<double>("strike"), Get<date>("maturity"), ...) or a list
    //! (Get<vector<double>>("weights"), Get<vector<date>>("dates"), ...) ---
    template <class T>
    T Get( const string& f )
    {
        const string p = Path( f );
        if constexpr ( std::is_same_v<T, double> )
            return _m.yml().GetDouble( p );
        else if constexpr ( std::is_same_v<T, int> )
            return _m.yml().GetInteger( p );
        else if constexpr ( std::is_same_v<T, long> )
            return _m.yml().GetLong( p );
        else if constexpr ( std::is_same_v<T, bool> )
            return _m.yml().GetBoolean( p );
        else if constexpr ( std::is_same_v<T, string> )
            return _m.yml().GetString( p );
        else if constexpr ( std::is_same_v<T, date> )
            return _m.yml().GetDate( p );
        else if constexpr ( std::is_same_v<T, vector<double>> )
            return _m.yml().GetDoubleList( p );
        else if constexpr ( std::is_same_v<T, vector<int>> )
            return _m.yml().GetIntegerList( p );
        else if constexpr ( std::is_same_v<T, vector<date>> )
            return _m.yml().GetDateList( p );
        else if constexpr ( std::is_same_v<T, vector<string>> )
            return _m.yml().GetStringList( p );
        else if constexpr ( std::is_same_v<T, vector<bool>> )
            return _m.yml().GetBooleanList( p );
        else
            static_assert( unsupported<T>, "ObjectReader::Get: unsupported type" );
    }

    //! --- scalar (optional, with a default) : Get<double>("notional", 1) ---
    template <class T>
    T Get( const string& f, const T& d )
    {
        const string p = Path( f );
        if constexpr ( std::is_same_v<T, double> )
            return _m.yml().GetDouble( p, d );
        else if constexpr ( std::is_same_v<T, int> )
            return _m.yml().GetInteger( p, d );
        else if constexpr ( std::is_same_v<T, long> )
            return _m.yml().GetLong( p, d );
        else if constexpr ( std::is_same_v<T, bool> )
            return _m.yml().GetBoolean( p, d );
        else if constexpr ( std::is_same_v<T, string> )
            return _m.yml().GetString( p, d );
        else if constexpr ( std::is_same_v<T, date> )
            return _m.yml().GetDate( p, d );
        else
            static_assert( unsupported<T>, "ObjectReader::Get: unsupported scalar type" );
    }

    //! the la_vector view is a raw pointer (not a vector<T>), so it stays named
    la_vector* LaVector( const string& f ) { return _m.yml().GetLaVector( Path( f ) ); }

    //! --- presence of an optional field, scalar (Has<string>("calendar")) or list
    //! (Has<vector<double>>("matrix")) ---
    template <class T>
    bool Has( const string& f )
    {
        const string p = Path( f );
        if constexpr ( std::is_same_v<T, string> )
            return _m.yml().IsString( p );
        else if constexpr ( std::is_same_v<T, double> )
            return _m.yml().IsDouble( p );
        else if constexpr ( std::is_same_v<T, int> )
            return _m.yml().IsInteger( p );
        else if constexpr ( std::is_same_v<T, bool> )
            return _m.yml().IsBoolean( p );
        else if constexpr ( std::is_same_v<T, vector<double>> )
            return _m.yml().IsDoubleList( p );
        else if constexpr ( std::is_same_v<T, vector<string>> )
            return _m.yml().IsStringList( p );
        else
            static_assert( unsupported<T>, "ObjectReader::Has: unsupported type" );
    }

    //! --- typed references: the field holds the *name(s)* of other object(s); each
    //! is got-or-built as the target type. Scalar (Ref<Currency>("rate") ->
    //! Currency*) or list (Ref<vector<Forex>>("forexs") -> vector<Forex*>).
    //! Resolution is type-dependent (see the class comment), hence the target type. ---
    template <class T>
    auto Ref( const string& f )
    {
        if constexpr ( reader_detail::is_vector<T> )
        {
            using E = typename T::value_type; //!< T = vector<E> -> vector<E*>
            return _m.GetList<E>( _m.yml().GetStringList( Path( f ) ) );
        }
        else
        {
            return _m.Get<T>( _m.yml().GetString( Path( f ) ) );
        }
    }

    //! --- escape hatch for the few irreducible cases (e.g. a factory that picks a
    //! concrete subclass before configuring) ---
    ObjectManager& Manager() { return _m; }
    const string& Name() const { return _n; }
};
