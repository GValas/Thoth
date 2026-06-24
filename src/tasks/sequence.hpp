#pragma once
#include "task.hpp"

class YamlConfig;

//! Write the sequence summary block (kind / task_time / tasks) under ResultBlock in
//! Cfg. Shared by Sequence::WriteResults (in-process) and the cluster master's
//! ClusterPriceSequence (which synthesises the same block from the slave responses),
//! so the two cannot drift out of byte-compatibility.
void WriteSequenceSummary( YamlConfig& Cfg,
                           const string& ResultBlock,
                           double TaskTime,
                           const vector<string>& TaskNames );

//! A task that runs a list of sub-tasks (typically pricers) one after another,
//! each writing its own result block. This lets a single configuration drive a
//! whole batch — for instance the full pricer/product matrix — in one run.
class Sequence : public Task
{

  private:
    vector<Task*> _tasks;       //!< sub-tasks, in execution order
    vector<string> _task_names; //!< their names, for the summary block

  public:
    //! read own field (the ordered list of sub-task references) + result block
    void Configure( ObjectReader& reader ) override;

    //! setter (resolved sub-tasks and their names, same order)
    void SetTaskList( const vector<Task*>& Tasks, const vector<string>& Names );

    void Execute() override;
    void WriteResults() override;

    Sequence( const string& ObjectName,
              YamlConfig& YamlConfig );
    ~Sequence() override;
};
