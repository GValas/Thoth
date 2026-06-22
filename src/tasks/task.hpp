#pragma once
#include "object.hpp"
#include "yaml_config.hpp"

//! Abstract executable unit (a pricer or a sequence): Execute() does the work and
//! WriteResults() writes its result block back into the YAML config.
class Task : public Object
{

  protected:
    //! attributes
    double _task_time = 0; //!< wall-clock seconds the task took (written into the result block)
    string _result;        //!< name of the YAML object the result block is written under

    //! non-owning handle to the run's config, so WriteResults can write this
    //! task's output back into the same document the engine was driven from
    YamlConfig* _cfg = nullptr;

    //! the "kind" tag stamped on the result block. Defaults to "<kind>_result"
    //! (e.g. sequence -> sequence_result); the pricer overrides it to a single
    //! "pricer_result" so the result schema does not fork per engine kind.
    virtual string ResultKind() const;

    //! write one field into this task's result block — the write-side mirror of
    //! ObjectReader::Get<T>: it prefixes _result so callers pass a bare field name
    //! (and the typed Set<T> dispatch lives once in YamlConfig).
    template <class T>
    void WriteResult( const string& Field, const T& Value )
    {
        _cfg->Set( _result + OBJECT_SEPARATOR + Field, Value );
    }

  public:
    //! setter (target object for this task's result block, read from "result")
    void SetResult( const string& Result );

    //! name of the object that receives this task's result block
    const string& GetResult() const { return _result; }

    //! task execution: Execute() runs the work (pricing / the sub-task batch),
    //! WriteResults() serialises its outputs into _cfg under _result. Kept separate
    //! so a Sequence can run a sub-task and persist its block before the next one
    //! re-prices any shared objects.
    virtual void Execute() = 0;
    virtual void WriteResults();

    //! constructor / destructor. ObjectKind is the concrete leaf kind (KIND_MCL_PRICER /
    //! KIND_SEQUENCE), forwarded to Object for the registry; YamlConfig is retained
    //! by pointer for WriteResults.
    Task( const string& ObjectName,
          YamlConfig& YamlConfig,
          const string& ObjectKind );
    ~Task() override;
};
