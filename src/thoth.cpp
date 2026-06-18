// Thoth.cpp : entry point for the console application. Parses the command line and
// dispatches to one of the run modes (each in its own translation unit, declared
// in run_modes.hpp: run_batch / run_server / run_client / run_cluster).
//

#include "thoth.hpp"
#include "run_modes.hpp"

#include <cstdlib>

// thoth running modes
enum RunningMode
{
    MODE_NULL,
    MODE_BATCH,
    MODE_SERVER,
    MODE_CLIENT,
    MODE_CLUSTER
};

static void DisplayHelp();
static RunningMode GetRunningMode( const int argc, const string& UserMode );

//! main
int main( int argc,
          char* argv[] )
{

    //! credits + build stamp (commit / compile time), so it is obvious which
    //! source a running or containerised server was built from
#ifndef THOTH_BUILD_ID
#define THOTH_BUILD_ID "dev"
#endif
    cout << "Thoth, build " << THOTH_BUILD_ID << ", compiled " << __DATE__ << " " << __TIME__ << endl
         << "================================================================== " << endl;

    //! no argument
    if ( argc == 1 )
    {
        DisplayHelp();
        return 0;
    }

    //! running mode
    switch ( GetRunningMode( argc, argv[1] ) )
    {
    case MODE_BATCH:
        //! -batch <input> <output> [exec_name]
        return RunBatch( argv[2], argv[3], argc >= 5 ? argv[4] : ROOT_NODE );

    case MODE_SERVER:
    {
        int port = std::atoi( argv[2] );
        if ( port <= 0 || port > 65535 )
        {
            cerr << "error: invalid port '" << argv[2] << "'" << endl;
            return 1;
        }
        return RunHttpServer( port );
    }

    case MODE_CLIENT:
        return RunClient( argv[2], argv[3] );

    case MODE_CLUSTER:
    {
        int port = std::atoi( argv[2] );
        if ( port <= 0 || port > 65535 )
        {
            cerr << "error: invalid port '" << argv[2] << "'" << endl;
            return 1;
        }
        vector<string> slaves( argv + 3, argv + argc ); //!< remaining args are slave URLs
        return RunClusterMaster( port, slaves );
    }

    case MODE_NULL:
    default:
        DisplayHelp();
        return 1;
    }
}

//! parsing command line
static RunningMode GetRunningMode( const int argc,
                                   const string& UserMode )
{
    if ( UserMode == "-batch" && argc >= 4 )
    {
        return MODE_BATCH;
    }
    else if ( UserMode == "-server" && argc >= 3 )
    {
        return MODE_SERVER;
    }
    else if ( UserMode == "-client" && argc >= 4 )
    {
        return MODE_CLIENT;
    }
    else if ( UserMode == "-cluster" && argc >= 4 )
    {
        return MODE_CLUSTER; //!< -cluster <port> <slave_url> [slave_url ...]
    }
    else
    {
        return MODE_NULL;
    }
}

//! display command line help
static void DisplayHelp()
{
    LOG( "HLP", "batch  : thoth -batch <input.yaml> <output.yaml> [exec_name]" );
    LOG( "HLP", "         price the task in input.yaml and write output.yaml" );
    cout << endl;
    LOG( "HLP", "server : thoth -server <port>" );
    LOG( "HLP", "         HTTP pricing service: POST /price (YAML body -> YAML result)," );
    LOG( "HLP", "         optional header X-Exec-Name; GET /health" );
    cout << endl;
    LOG( "HLP", "client : thoth -client <url> <input.yaml>" );
    LOG( "HLP", "         POST input.yaml to a thoth server and print the result" );
    cout << endl;
    LOG( "HLP", "cluster: thoth -cluster <port> <slave_url> [slave_url ...]" );
    LOG( "HLP", "         master that splits an MCL book's paths across slave -server" );
    LOG( "HLP", "         instances, dispatches over HTTP and aggregates the results" );
    cout << endl;
}
