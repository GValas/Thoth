#include "run_modes.hpp"
#include "object_manager.hpp"

//! launch a single pricing task from files
int RunBatch( const string& InputFile,
              const string& OutputFile,
              const string& TaskName )
{

    try
    {
        double t0 = WallClockSeconds();
        ObjectManager ObjManager( InputFile, OutputFile );
        LOG( "CFG", "config read, task_time = " + TaskTimeLog( t0 ) );

        t0 = WallClockSeconds();
        ObjManager.ReadObjects( TaskName );
        LOG( "INI", "objects created, " + TaskTimeLog( t0 ) );

        t0 = WallClockSeconds();
        ObjManager.ExecuteTask();
        LOG( "TSK", "executed objects, " + TaskTimeLog( t0 ) );

        t0 = WallClockSeconds();
        ObjManager.WriteResults();
        ObjManager.WriteOutputFile(); //!< explicit flush (instead of in ~YamlConfig)
        LOG( "OUT", "written outputs, " + TaskTimeLog( t0 ) );
    }
    //! error while executing task
    catch ( std::exception& e )
    {
        cout << endl
             << e.what() << endl;
        return 1;
    }
    catch ( ... )
    {
        cout << "unknown error while executing task" << endl;
        return 1;
    }
    return 0;
}
