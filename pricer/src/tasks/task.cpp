#include "thoth.hpp"
#include "task.hpp"
#include "result_schema.hpp" //!< canonical result field names

//! retain the config by pointer (not owned) so WriteResults can write back into
//! the same document; the kind is forwarded to Object for registry construction.
Task::Task( const string& ObjectName,
            YamlConfig& YamlConfig,
            const string& ObjectKind ) : Object( ObjectName, ObjectKind )
{
    _cfg = &YamlConfig;
}

//! nothing owned here (the config is borrowed), so the default destructor suffices
Task::~Task() = default;

//! record the name of the object that will receive this task's result block
void Task::SetResult( const string& Result )
{
    _result = Result;
}

//! base result block shared by every task: tag the block with its kind (ResultKind)
//! and stamp the elapsed wall-clock time. Concrete tasks call this, then add their own fields.
void Task::WriteResults()
{
    WriteResult( result_schema::KIND, ResultKind() );
    WriteResult( result_schema::TASK_TIME, _task_time );
}

//! default result-block kind: the task's own kind suffixed with "_result"
string Task::ResultKind() const
{
    return _kind + "_result";
}