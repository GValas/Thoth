#pragma once
#include "task.hpp"

//! A task that runs a list of sub-tasks (typically pricers) one after another,
//! each writing its own result block. This lets a single configuration drive a
//! whole batch — for instance the full pricer/product matrix — in one run.
class Sequence : public Task
{

  private:
    vector<Task*> _tasks;       //!< sub-tasks, in execution order
    vector<string> _task_names; //!< their names, for the summary block

  public:
    //! setter (resolved sub-tasks and their names, same order)
    void SetTaskList( const vector<Task*>& Tasks, const vector<string>& Names );

    void Execute() override;
    void WriteResults() override;

    Sequence( const string& ObjectName,
              YamlConfig& YamlConfig );
    ~Sequence() override;
};
