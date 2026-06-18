#include "run_modes.hpp"
#include "object_manager.hpp"

//! launch a single pricing task from files
int RunBatch( const string& InputFile,
              const string& OutputFile,
              const string& ExecName )
{

    try
    {
        double t0 = WallClockSeconds();
        ObjectManager ObjManager( InputFile, OutputFile );
        LOG( "CFG", "config read, exec_time = " + ExecTimeLog( t0 ) );

        t0 = WallClockSeconds();
        ObjManager.ReadObjects( ExecName );
        LOG( "INI", "objects created, " + ExecTimeLog( t0 ) );

        t0 = WallClockSeconds();
        ObjManager.ExecuteTask();
        LOG( "EXE", "executed objects, " + ExecTimeLog( t0 ) );

        t0 = WallClockSeconds();
        ObjManager.WriteResults();
        ObjManager.WriteOutputFile(); //!< explicit flush (instead of in ~YamlConfig)
        LOG( "OUT", "written outputs, " + ExecTimeLog( t0 ) );
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
