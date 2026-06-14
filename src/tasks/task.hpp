#pragma once
#include "object.hpp"
#include "yaml_config.hpp"

class Task : public Object
{

  protected:
    //! attributes
    double _exec_time = 0;
    string _result;

    //! enables to write results
    YamlConfig* _cfg = nullptr;

  public:
    //! setter
    void SetResult( const string& Result );

    //! name of the object that receives this task's result block
    const string& GetResult() const { return _result; }

    //! task execution
    virtual void Execute() = 0;
    virtual void WriteResults();

    //! constructor / destructor
    Task( const string& ObjectName,
          YamlConfig& YamlConfig,
          const string& ObjectKind );
    ~Task() override;
};
