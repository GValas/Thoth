#include "thoth.hpp"
#include "task.hpp"

//!
Task::Task( const string& ObjectName,
            YamlConfig& YamlConfig,
            const string& ObjectKind ) : Object( ObjectName, ObjectKind )
{
    _cfg = &YamlConfig;
}

//!
Task::~Task() = default;

//!
void Task::SetResult( const string& Result )
{
    _result = Result;
}

//!
void Task::WriteResults()
{
    _cfg->SetString( _result + ".kind", _kind + "_result" );
    _cfg->SetDouble( _result + ".exec_time", _exec_time );
}