//! ----------------------------------------------------------------------------
//! run_batch.cpp : the `-batch` run mode — the simplest entry point. It prices a
//! single book file in-process and writes the result to another file, with no
//! networking. Each pipeline stage is timed and logged so a run's wall-clock
//! profile (parse / build / price / write) is visible on the console.
//! ----------------------------------------------------------------------------

#include "run_modes.hpp"
#include "object_manager.hpp"

//! launch a single pricing task from files.
//!   InputFile  : YAML book to read
//!   OutputFile : YAML file to write the result block(s) to
//!   TaskName   : task to run (ROOT_NODE = whatever the book's `root:` selects)
//! Returns 0 on success, 1 if any stage throws. Each of the four stages is timed
//! from a fresh t0 and reported via LOG.
int RunBatch( const string& InputFile,
              const string& OutputFile,
              const string& TaskName )
{

    try
    {
        //! stage 1 : parse the YAML book into the config tree
        double t0 = WallClockSeconds();
        ObjectManager ObjManager( InputFile, OutputFile );
        LOG( "CFG", "config read, task_time = " + TaskTimeLog( t0 ) );

        //! stage 2 : materialise the C++ object graph for the requested task
        t0 = WallClockSeconds();
        ObjManager.ReadObjects( TaskName );
        LOG( "INI", "objects created, " + TaskTimeLog( t0 ) );

        //! stage 3 : run the task (the actual pricing/computation)
        t0 = WallClockSeconds();
        ObjManager.ExecuteTask();
        LOG( "TSK", "executed objects, " + TaskTimeLog( t0 ) );

        //! stage 4 : serialise results into the config tree and flush to disk
        t0 = WallClockSeconds();
        ObjManager.WriteResults();
        ObjManager.WriteOutputFile(); //!< explicit flush (instead of in ~YamlConfig)
        LOG( "OUT", "written outputs, " + TaskTimeLog( t0 ) );
    }
    //! error while executing task : print the message and return a non-zero exit
    //! code so a calling script can detect the failure.
    catch ( std::exception& e )
    {
        cout << endl
             << e.what() << endl;
        return 1;
    }
    catch ( ... ) //!< non-std exception : still fail cleanly rather than terminate
    {
        cout << "unknown error while executing task" << endl;
        return 1;
    }
    return 0;
}
