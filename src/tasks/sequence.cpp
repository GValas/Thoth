#include "thoth.hpp"
#include "sequence.hpp"

//! constructor
Sequence::Sequence( const string& ObjectName,
                    YamlConfig& YamlConfig ) : Task( ObjectName, YamlConfig, KIND_SEQUENCE ) {}

//! destructor
Sequence::~Sequence() = default;

//! setter
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
    double t0 = WallClockSeconds();
    for ( size_t i = 0; i < _tasks.size(); i++ )
    {
        LOG( "SEQ", "task " + ToString( i + 1 ) + "/" + ToString( _tasks.size() ) + " : " + _task_names[i],
             LOG_COLOR_CYAN );
        _tasks[i]->Execute();
        _tasks[i]->WriteResults();
    }
    _exec_time = ExecTime( t0 );
    LOG( "SEQ", "ran " + ToString( _tasks.size() ) + " task(s), exec_time = " + ToString( _exec_time ) + " sec" );
}

//! sub-task results are written as they run (see Execute); only the summary
//! block is left for here.
void Sequence::WriteResults()
{
    if ( !_result.empty() )
    {
        _cfg->SetString( _result + ".kind", _kind + "_result" );
        _cfg->SetDouble( _result + ".exec_time", _exec_time );
        _cfg->SetStringList( _result + ".tasks", _task_names );
    }
}
