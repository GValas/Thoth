#include "thoth.hpp"
#include "sequence.hpp"
#include "object_reader.hpp"

//! constructor: a sequence is a Task of kind KIND_SEQUENCE (the sub-tasks it owns
//! are resolved later in Configure, not here)
Sequence::Sequence( const string& ObjectName,
                    YamlConfig& YamlConfig ) : Task( ObjectName, YamlConfig, KIND_SEQUENCE ) {}

//! destructor: the sub-tasks are owned by the ObjectManager, not the sequence, so
//! nothing to free here
Sequence::~Sequence() = default;

//! resolve the referenced sub-tasks (built and configured on demand) and the
//! object that receives this sequence's summary result block
void Sequence::Configure( ObjectReader& reader )
{
    const vector<string> names = reader.Get<vector<string>>( "tasks" );
    SetTaskList( reader.Manager().GetList<Task>( names ), names );
    SetResult( reader.Get<string>( "result", "" ) );
}

//! setter: store the resolved sub-tasks and their names (kept in the same order so
//! the summary block lists them in execution order)
void Sequence::SetTaskList( const vector<Task*>& Tasks, const vector<string>& Names )
{
    _tasks = Tasks;
    _task_names = Names;
}

//! run every sub-task in order, writing each task's result block immediately so
//! that tasks sharing objects (e.g. several pricers on one book) each capture
//! their own numbers before the next task re-prices the shared state.
void Sequence::Execute()
{
    double t0 = WallClockSeconds(); //!< start the wall clock for the whole batch
    for ( size_t i = 0; i < _tasks.size(); i++ )
    {
        LOG( "SEQ", "task " + ToString( i + 1 ) + "/" + ToString( _tasks.size() ) + " : " + _task_names[i] );
        _tasks[i]->Execute();      //!< run the sub-task (e.g. price the book)
        _tasks[i]->WriteResults(); //!< persist its block NOW, before the next task touches shared state
    }
    _task_time = TaskTime( t0 ); //!< total elapsed time for the sequence
    LOG( "SEQ", "ran " + ToString( _tasks.size() ) + " task(s), task_time = " + ToString( _task_time ) + " sec" );
}

//! sub-task results are written as they run (see Execute); only the summary
//! block is left for here.
void Sequence::WriteResults()
{
    if ( !_result.empty() )
    {
        _cfg->Set( _result + ".kind", _kind + "_result" );
        _cfg->Set( _result + ".task_time", _task_time );
        _cfg->Set( _result + ".tasks", _task_names );
    }
}
