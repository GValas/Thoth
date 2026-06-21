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

//! init objects tree (the task node is the task named by _task_name)
void ObjectManager::ReadObjects( const string& TaskName )
{
    //! parsing task (root is special _name)
    _task_name = ( TaskName == ROOT_NODE ) ? _yml.GetString( ROOT_NODE ) : TaskName;
    _task_node = Get<Task>( _task_name );

    //! getting task result node
    if ( _task_node )
    {
        _task_node->SetResult( _yml.GetString( _task_name + ".result" ) );
    }
}

//! execute task
void ObjectManager::ExecuteTask()
{
    LOG( "TSK", "executing task : " + _task_name );

    //! executing root
    if ( _task_node )
    {
        //! total wall-clock time for the whole task (real elapsed, not CPU time)
        const double t0 = WallClockSeconds();
        _task_node->Execute();

        //! system_information
        _yml.Set( "system_information.last_task_name", _task_node->GetName() );
        _yml.Set( "system_information.last_task_kind", _task_node->GetKind() );
        _yml.Set( "system_information.task_time", TaskTime( t0 ) );
    }

    //! no task to execute
    else
    {
        ERR( _task_name + " is not a task" );
    }
}

//! export results
void ObjectManager::WriteResults()
{
    //! executing root
    if ( _task_node )
    {
        _task_node->WriteResults();
    }
}

//! flush the config tree to the output file (batch mode)
void ObjectManager::WriteOutputFile()
{
    _yml.WriteFile();
}
