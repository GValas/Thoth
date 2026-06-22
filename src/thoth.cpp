// Thoth.cpp : entry point for the console application. Parses the command line and
// dispatches to one of the run modes (each in its own translation unit, declared
// in run_modes.hpp: run_batch / run_server / run_client / run_cluster).
//
// This file owns only the CLI surface : the build banner, the usage/help text,
// argv classification (GetRunningMode) and the dispatch switch in main(). All the
// actual work lives behind the Run* entry points; main() does no pricing itself.
//

#include "thoth.hpp"
#include "run_modes.hpp"

#include <cstdlib>

// thoth running modes : one per CLI sub-command. MODE_NULL means "no recognised
// mode / too few args" and routes to the help screen.
enum RunningMode
{
    MODE_NULL,   //!< unrecognised command or missing arguments -> show help
    MODE_BATCH,  //!< -batch  : price a file in process
    MODE_SERVER, //!< -server : HTTP pricing daemon
    MODE_CLIENT, //!< -client : POST a file to a server
    MODE_CLUSTER //!< -cluster: master that path-splits across slave servers
};

static void DisplayHelp();
//! classify argv[1] (and the arg count) into a RunningMode; pure parsing, no I/O
static RunningMode GetRunningMode( const int argc, const string& UserMode );

//! main : print the build banner, then parse argv[1] into a run mode and forward
//! the trailing positional args to the matching Run* entry point. Returns that
//! entry point's exit code (0 on success), or 1 on a parse/validation failure.
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

    //! running mode : argv[1] selects the mode, argv[2..] are its operands. The
    //! arg count was already validated per-mode in GetRunningMode, so the indexed
    //! accesses below are safe for the mode that was returned.
    switch ( GetRunningMode( argc, argv[1] ) )
    {
    case MODE_BATCH:
        //! -batch <input> <output> [task_name] ; default the optional task to the
        //! document root (price whatever the book's `root:` points at)
        return RunBatch( argv[2], argv[3], argc >= 5 ? argv[4] : ROOT_NODE );

    case MODE_SERVER:
    {
        //! -server <port> ; atoi yields 0 on a non-numeric arg, caught by the range check
        int port = std::atoi( argv[2] );
        if ( port <= 0 || port > 65535 )
        {
            cerr << "error: invalid port '" << argv[2] << "'" << endl;
            return 1;
        }
        return RunHttpServer( port );
    }

    case MODE_CLIENT:
        //! -client <url> <input.yaml>
        return RunClient( argv[2], argv[3] );

    case MODE_CLUSTER:
    {
        //! -cluster <port> <slave_url ...> ; same port validation as -server
        int port = std::atoi( argv[2] );
        if ( port <= 0 || port > 65535 )
        {
            cerr << "error: invalid port '" << argv[2] << "'" << endl;
            return 1;
        }
        vector<string> slaves( argv + 3, argv + argc ); //!< everything past the port is a slave URL
        return RunClusterMaster( port, slaves );
    }

    case MODE_NULL:
    default:
        //! unknown command / too few args : print usage and signal failure
        DisplayHelp();
        return 1;
    }
}

//! parsing command line : map the mode flag to a RunningMode, requiring the
//! minimum arg count each mode needs (argc counts argv[0], so e.g. -batch needs
//! the flag + input + output = 4). Anything else -> MODE_NULL (help).
static RunningMode GetRunningMode( const int argc,
                                   const string& UserMode )
{
    if ( UserMode == "-batch" && argc >= 4 ) //!< flag + input + output (task optional)
    {
        return MODE_BATCH;
    }
    else if ( UserMode == "-server" && argc >= 3 ) //!< flag + port
    {
        return MODE_SERVER;
    }
    else if ( UserMode == "-client" && argc >= 4 ) //!< flag + url + input
    {
        return MODE_CLIENT;
    }
    else if ( UserMode == "-cluster" && argc >= 4 ) //!< flag + port + >=1 slave url
    {
        return MODE_CLUSTER; //!< -cluster <port> <slave_url> [slave_url ...]
    }
    else
    {
        return MODE_NULL; //!< unrecognised flag or not enough operands
    }
}

//! display command line help : one usage block per mode, emitted via LOG so it
//! shares the timestamp/tag formatting of the rest of the console output.
static void DisplayHelp()
{
    LOG( "HLP", "batch  : thoth -batch <input.yaml> <output.yaml> [task_name]" );
    LOG( "HLP", "         price the task in input.yaml and write output.yaml" );
    cout << endl;
    LOG( "HLP", "server : thoth -server <port>" );
    LOG( "HLP", "         HTTP pricing service: POST /price (YAML body -> YAML result)," );
    LOG( "HLP", "         optional header X-Task-Name; GET /health" );
    cout << endl;
    LOG( "HLP", "client : thoth -client <url> <input.yaml>" );
    LOG( "HLP", "         POST input.yaml to a thoth server and print the result" );
    cout << endl;
    LOG( "HLP", "cluster: thoth -cluster <port> <slave_url> [slave_url ...]" );
    LOG( "HLP", "         master that splits an MCL book's paths across slave -server" );
    LOG( "HLP", "         instances, dispatches over HTTP and aggregates the results" );
    cout << endl;
}
