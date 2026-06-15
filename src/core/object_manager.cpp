#include "thoth.hpp"
#include "object_manager.hpp"

//! constructor
ObjectManager::ObjectManager( const string& InputFile,
                              const string& OutputFile )
{
    _c = std::make_unique<YamlConfig>( InputFile, OutputFile );
}

//! in-memory constructor (HTTP request body)
ObjectManager::ObjectManager( YamlConfig::from_string_t,
                              const string& YamlContent )
{
    _c = std::make_unique<YamlConfig>( YamlConfig::from_string_t{}, YamlContent );
}

//! results as a YAML string
string ObjectManager::ResultYaml()
{
    return _c->Dump();
}

//! destructor
ObjectManager::~ObjectManager() = default;

//! an object exists iff it carries a kind tag (e.g. `name: !equity { ... }`)
bool ObjectManager::IsObject( const string& Path )
{
    return !_c->GetTag( Path ).empty();
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
    _exec_name = ( ExecName == ROOT_NODE ) ? _c->GetString( ROOT_NODE ) : ExecName;
    _exec_node = IsObject( _exec_name ) ? Get<Task>( _exec_name ) : nullptr;

    //! getting exec result node
    if ( _exec_node )
    {
        _exec_node->SetResult( _c->GetString( _exec_name + ".result" ) );
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
        _c->SetString( "system_information.last_exec_name", _exec_node->GetName() );
        _c->SetString( "system_information.last_exec_kind", _exec_node->GetKind() );
        _c->SetDouble( "system_information.exec_time", ExecTime( t0 ) );
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
    _c->WriteFile();
}
