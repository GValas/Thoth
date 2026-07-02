#pragma once
#include "object_collector.hpp"
#include "yaml_config.hpp"
#include "task.hpp"

#include <map>

//! object_manager.hpp — orchestrates loading, building, running and emitting a book.
//! It owns the YamlConfig and the ObjectCollector, and is the get-or-build entry
//! point (Get<T>/GetList<T>) that the per-type factories use to resolve references.

//! ROOT_NODE is defined in Constants.hpp (inline constexpr)

//! Builds the object graph from the parsed YAML.
//!
//! The manager is deliberately type-agnostic: it knows nothing about the
//! concrete object classes. Each kind tag maps to a factory in a single table
//! (object_registry.cpp) — adding a new object type means adding one entry
//! there and a new class, with no change here. Factories create their product,
//! register it in the collector and configure it (resolving referenced objects
//! through Get<T>).
class ObjectManager
{
  public:
    //! lifecycle
    void ReadObjects( const string& TaskName ); //!< resolve the root task
    void ExecuteTask();                         //!< run it
    void WriteResults();                        //!< export results into the config tree
    void WriteOutputFile();                     //!< flush the config tree to the output file

    //! result config emitted as a YAML string (in-memory / HTTP mode)
    string ResultYaml();

    //! constructors: the manager owns its YamlConfig, built from files or (for an
    //! in-memory HTTP request) from a YAML string.
    ObjectManager( const string& InputFile, const string& OutputFile );
    ObjectManager( YamlConfig::from_string_t, const string& YamlContent );
    ~ObjectManager();

    //! non-copyable / non-movable: built objects capture the address of the
    //! owned-by-value _yml (Task holds a YamlConfig*), so a copy or move would
    //! leave them pointing into the abandoned instance.
    ObjectManager( const ObjectManager& ) = delete;
    ObjectManager& operator=( const ObjectManager& ) = delete;
    ObjectManager( ObjectManager&& ) = delete;
    ObjectManager& operator=( ObjectManager&& ) = delete;

    //! --- services used by the registry factories ---

    //! config tree (field access)
    YamlConfig& yml() { return _yml; }

    //! object store (factories Add/Own their product here)
    ObjectCollector& collector() { return _collector; }

    //! get-or-build the object named ObjectName, viewed as the (base) type T.
    //! Returns the cached object if already built, else dispatches on its kind
    //! tag to the registry factory. Errors if it is missing or not a T.
    template <class T>
    T* Get( const string& ObjectName )
    {
        CheckObject( ObjectName ); //!< error early if the name is not an object node
        //! fast path: already built and registered — return the cached pointer,
        //! viewed as T (so the same object can be shared across many references).
        if ( T* cached = _collector.Get<T>( ObjectName ) )
        {
            return cached;
        }
        //! Build returns the bare Object (the registry is the only TU that knows the
        //! concrete types); the T-dependent dynamic_cast stays here, where T is complete.
        if ( T* built = dynamic_cast<T*>( Build( ObjectName ) ) )
        {
            return built;
        }
        ERR( "object '" + ObjectName + "' has an unexpected type for this reference" );
    }

    //! map a list of names through Get<T> — get-or-build each referenced object as
    //! a T*, preserving order (used for list-valued references, e.g. basket members).
    template <class T>
    vector<T*> GetList( const vector<string>& ObjectNameList )
    {
        vector<T*> result;
        result.reserve( ObjectNameList.size() );
        for ( const auto& name : ObjectNameList )
        {
            result.push_back( Get<T>( name ) );
        }
        return result;
    }

  private:
    YamlConfig _yml;            //!< owned config tree (fields, tags, results)
    Task* _task_node = nullptr; //!< the resolved root task; null until ReadObjects
    string _task_name;          //!< name of the task being executed
    ObjectCollector _collector; //!< owns every built object (keyed by name)

    //! an object exists iff it carries a kind tag (e.g. `name: !equity { ... }`)
    bool IsObject( const string& Path );
    void CheckObject( const string& ObjectName ); //!< IsObject + ERR on miss

    //! dispatch ObjectName's kind tag to its factory. Defined in
    //! object_registry.cpp, the single translation unit aware of every type.
    Object* Build( const string& ObjectName );
};
