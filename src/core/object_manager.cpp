//! object_manager.cpp — driver that turns a parsed YAML book into a runnable task.
//! Lifecycle: ReadObjects (resolve the root task object) -> ExecuteTask (run it,
//! timing the whole sweep) -> WriteResults / WriteOutputFile (emit results back
//! into the config tree, then to disk or to an in-memory YAML string).
#include "thoth.hpp"
#include "object_manager.hpp"

//! constructor: built from files; the manager owns the config. _yml opens the
//! input file now and will write to OutputFile in WriteOutputFile (batch mode).
ObjectManager::ObjectManager( const string& InputFile, const string& OutputFile )
    : _yml( InputFile, OutputFile )
{
}

//! in-memory constructor (HTTP request body): the YAML arrives as a string rather
//! than a file path; results are returned via ResultYaml instead of a file.
ObjectManager::ObjectManager( YamlConfig::from_string_t, const string& YamlContent )
    : _yml( YamlConfig::from_string_t{}, YamlContent )
{
}

//! results as a YAML string — serialise the (now result-populated) config tree.
//! Used by the HTTP path to send the answer back in the response body.
string ObjectManager::ResultYaml()
{
    return _yml.Dump();
}

//! destructor — defaulted; _yml and _collector clean up their owned state.
ObjectManager::~ObjectManager() = default;

//! an object exists iff it carries a kind tag (e.g. `name: !equity { ... }`).
//! A plain scalar/field has no tag, so a missing or untagged path is "not an object".
bool ObjectManager::IsObject( const string& Path )
{
    return !_yml.GetTag( Path ).empty();
}

//! guard: fail loudly if a referenced name does not resolve to an object node.
//! Called before every get-or-build so a typo'd reference errors with the name
//! rather than surfacing later as a null pointer.
void ObjectManager::CheckObject( const string& ObjectName )
{
    if ( !IsObject( ObjectName ) )
    {
        ERR( ObjectName + " does not exist" );
    }
}

//! init objects tree (the task node is the task named by _task_name).
//! Resolving the root Task triggers the whole get-or-build cascade: configuring
//! the task pulls in every object it references, so the object graph is built lazily.
void ObjectManager::ReadObjects( const string& TaskName )
{
    //! parsing task (root is special _name): when called with ROOT_NODE, the actual
    //! task name is read from the top-level ROOT_NODE field; otherwise use it directly.
    _task_name = ( TaskName == ROOT_NODE ) ? _yml.GetString( ROOT_NODE ) : TaskName;
    _task_node = Get<Task>( _task_name ); //!< builds the task and, transitively, its tree

    //! getting task result node — tell the task where in the config tree to write
    //! its output block, so WriteResults later lands the results at the right path.
    if ( _task_node )
    {
        _task_node->SetResult( _yml.GetString( _task_name + ".result" ) );
    }
}

//! execute task — run the resolved root task and stamp run metadata into the config.
void ObjectManager::ExecuteTask()
{
    LOG( "TSK", "executing task : " + _task_name );

    //! executing root
    if ( _task_node )
    {
        //! total wall-clock time for the whole task (real elapsed, not CPU time):
        //! capture the start, run, then record the elapsed span below.
        const double t0 = WallClockSeconds();
        _task_node->Execute();

        //! system_information — provenance written back into the result tree so the
        //! caller can see which task ran and how long the whole sweep took.
        _yml.Set( "system_information.last_task_name", _task_node->GetName() );
        _yml.Set( "system_information.last_task_kind", _task_node->GetKind() );
        _yml.Set( "system_information.task_time", TaskTime( t0 ) );
    }

    //! the named node resolved but was not a Task (or none was found): refuse to run.
    else
    {
        ERR( _task_name + " is not a task" );
    }
}

//! export results — let the task serialise its computed outputs into the config tree
//! (in memory). Separate from WriteOutputFile so the HTTP path can dump to a string.
void ObjectManager::WriteResults()
{
    //! executing root
    if ( _task_node )
    {
        _task_node->WriteResults();
    }
}

//! flush the config tree to the output file (batch mode). The HTTP path skips this
//! and calls ResultYaml instead.
void ObjectManager::WriteOutputFile()
{
    _yml.WriteFile();
}
