#include "thoth.hpp"
#include "object_manager.hpp"

//! constructor: built from files; the manager owns the config
ObjectManager::ObjectManager( const string& InputFile, const string& OutputFile )
    : _yml( InputFile, OutputFile )
{
}

//! in-memory constructor (HTTP request body)
ObjectManager::ObjectManager( YamlConfig::from_string_t, const string& YamlContent )
    : _yml( YamlConfig::from_string_t{}, YamlContent )
{
}

//! results as a YAML string
string ObjectManager::ResultYaml()
{
    return _yml.Dump();
}

//! destructor
ObjectManager::~ObjectManager() = default;

//! an object exists iff it carries a kind tag (e.g. `name: !equity { ... }`)
bool ObjectManager::IsObject( const string& Path )
{
    return !_yml.GetTag( Path ).empty();
}

//!
void ObjectManager::CheckObject( const string& ObjectName )
{
    if ( !IsObject( ObjectName ) )
    {
        ERR( ObjectName + " does not exist" );
    }
}

//! init objects tree (the exec node is the task named by _exec_name)
void ObjectManager::ReadObjects( const string& ExecName )
{
    //! parsing task (root is special _name)
    _exec_name = ( ExecName == ROOT_NODE ) ? _yml.GetString( ROOT_NODE ) : ExecName;
    _exec_node = Get<Task>( _exec_name );

    //! getting exec result node
    if ( _exec_node )
    {
        _exec_node->SetResult( _yml.GetString( _exec_name + ".result" ) );
    }
}

//! execute task
void ObjectManager::ExecuteTask()
{
    LOG( "EXE", "executing task : " + _exec_name );

    //! executing root
    if ( _exec_node )
    {
        //! total wall-clock time for the whole task (real elapsed, not CPU time)
        const double t0 = WallClockSeconds();
        _exec_node->Execute();

        //! system_information
        _yml.SetString( "system_information.last_exec_name", _exec_node->GetName() );
        _yml.SetString( "system_information.last_exec_kind", _exec_node->GetKind() );
        _yml.SetDouble( "system_information.exec_time", ExecTime( t0 ) );
    }

    //! no task to execute
    else
    {
        ERR( _exec_name + " is not a task" );
    }
}

//! export results
void ObjectManager::WriteResults()
{
    //! executing root
    if ( _exec_node )
    {
        _exec_node->WriteResults();
    }
}

//! flush the config tree to the output file (batch mode)
void ObjectManager::WriteOutputFile()
{
    _yml.WriteFile();
}
