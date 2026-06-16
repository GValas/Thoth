// Thoth.cpp : entry point for the console application.
//

#include "thoth.hpp"
#include "object_manager.hpp"
#include "cancellation.hpp"
#include "progress_bar.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <httplib.h>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

// thoth running modes
enum RunningMode
{
    MODE_NULL,
    MODE_BATCH,
    MODE_SERVER,
    MODE_CLIENT,
    MODE_CLUSTER
};

//! declarations (the run functions return a process exit code)
void DisplayHelp();
int RunBatch( const string& InputFile, const string& OutputFile, const string& ExecName );
int RunHttpServer( int Port );
int RunClient( const string& Url, const string& InputFile );
int RunClusterMaster( int Port, const vector<string>& Slaves );
RunningMode GetRunningMode( const int argc, const string& UserMode );

//! execute an in-memory YAML request (any task, not only pricing) and return
//! the YAML result (throws on error)
static string ExecuteYaml( const string& YamlRequest, const string& ExecName )
{
    ObjectManager manager( YamlConfig::from_string_t{}, YamlRequest );
    manager.ReadObjects( ExecName );
    manager.ExecuteTask();
    manager.WriteResults();
    return manager.ResultYaml();
}

//! main
int main( int argc,
          char* argv[] )
{
    //! disable gsl errors
    gsl_set_error_handler_off();

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

//! HTTP pricing server : POST /price (YAML in -> YAML out), GET /health
int RunHttpServer( int Port )
{
    httplib::Server server;

    //! pricing is serialised by price_mutex below, so a slave only ever handles
    //! one price at a time; cap the worker pool small (default is ~one thread per
    //! core) so a many-slave cluster on one box does not spawn hundreds of idle
    //! threads at startup (which made high slave counts fail to come up in time).
    server.new_task_queue = [] { return new httplib::ThreadPool( 4 ); };

    //! pricing is not re-entrant (global GSL state) -> serialise requests
    static std::mutex price_mutex;

    server.Get( "/health", []( const httplib::Request&, httplib::Response& res )
                { res.set_content( "ok\n", "text/plain" ); } );

    //! progress of the in-flight pricing : "<current> <total> <active 0|1>". Reads
    //! only atomics (no price_mutex), so it answers while a price runs — this is
    //! what the cluster master polls to draw an approximate global progress bar.
    server.Get( "/progress", []( const httplib::Request&, httplib::Response& res )
                {
        const GlobalProgress& g = global_progress();
        std::ostringstream oss;
        oss << g.current.load() << " " << g.total.load() << " " << ( g.active.load() ? 1 : 0 );
        res.set_content( oss.str(), "text/plain" ); } );

    server.Post( "/price", []( const httplib::Request& req, httplib::Response& res )
                 {
        //! copy what the worker needs : the provider runs after this returns
        const string body = req.body;
        const string exec_name = req.has_header( "X-Exec-Name" )
                                 ? req.get_header_value( "X-Exec-Name" )
                                 : string( ROOT_NODE );
        const string client = req.remote_addr + ":" + std::to_string( req.remote_port );
        LOG( "HTTP", "client " + client + " connected (price request)" );

        //! Stream the result through a chunked provider so we keep a live handle
        //! on the connection (sink.is_writable) while pricing runs in a worker
        //! thread. If the client disconnects we raise the cancellation flag and
        //! the pricing loop bails out, freeing the server instead of computing
        //! a result nobody will read.
        res.set_chunked_content_provider(
            "application/x-yaml",
            [body, exec_name, client]( size_t /*offset*/, httplib::DataSink& sink ) -> bool
            {
                //! one pricing at a time (global GSL state) -> serialise end to end.
                //! if another request holds the lock, this client is queued : say so.
                std::unique_lock<std::mutex> lock( price_mutex, std::try_to_lock );
                if ( !lock.owns_lock() )
                {
                    LOG( "HTTP", "client " + client + " queued (a pricing is already running)" );
                    lock.lock();
                }
                cancellation::Reset();

                string result;
                std::atomic<bool> done{ false };
                std::thread worker( [&]()
                {
                    try { result = ExecuteYaml( body, exec_name ); }
                    catch ( const std::exception& e ) { result = string( "error: " ) + e.what() + "\n"; }
                    catch ( ... ) { result = "error: unknown failure\n"; }
                    done.store( true );
                } );

                //! poll the connection while the worker prices
                while ( !done.load() )
                {
                    if ( !sink.is_writable() ) //!< client gone
                    {
                        LOG( "HTTP", "client disconnected : cancelling pricing" );
                        cancellation::Request();
                        break;
                    }
                    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
                }
                worker.join();

                if ( cancellation::Requested() )
                {
                    return false; //!< client disconnected : abort, nothing to send
                }

                sink.write( result.data(), result.size() );
                sink.done();
                return true;
            } ); } );

    cout << "Thoth HTTP server listening on 0.0.0.0:" << Port
         << "  (POST /price, GET /health)" << endl;

    if ( !server.listen( "0.0.0.0", Port ) )
    {
        cerr << "error: could not bind port " << Port << endl;
        return 1;
    }
    return 0;
}

//! ---------------------------------------------------------------------------
//! Cluster mode : a master server splits a Monte-Carlo book's paths across a
//! set of slave servers (ordinary `-server` instances), dispatches the
//! sub-requests over HTTP, and aggregates the results. Each slave gets a
//! distinct random-stream seed so the paths are disjoint and the pooled
//! estimate genuinely averages independent samples.
//! ---------------------------------------------------------------------------

//! POST a YAML body to one slave and return its response body (throws on failure)
static string PostToSlave( const string& Url,
                           const string& Body,
                           const string& ExecName )
{
    httplib::Client client( Url );
    client.set_read_timeout( 3600 );
    client.set_write_timeout( 60 );

    httplib::Headers headers;
    if ( ExecName != ROOT_NODE )
    {
        headers.emplace( "X-Exec-Name", ExecName );
    }

    auto res = client.Post( "/price", headers, Body, "application/x-yaml" );
    if ( !res )
    {
        throw std::runtime_error( "slave " + Url + " unreachable (" + httplib::to_string( res.error() ) + ")" );
    }
    if ( res->status != 200 || res->body.rfind( "error: ", 0 ) == 0 )
    {
        throw std::runtime_error( "slave " + Url + " returned: " + res->body );
    }
    return res->body;
}

//! GET a slave's /progress ("<current> <total> <active>"); false on any failure
//! (unreachable, timeout, old slave without the endpoint) so polling degrades
//! gracefully instead of stalling the master.
static bool PollSlaveProgress( const string& Url, long& Current, long& Total, bool& Active )
{
    httplib::Client client( Url );
    client.set_connection_timeout( 1 );
    client.set_read_timeout( 2 );
    auto res = client.Get( "/progress" );
    if ( !res || res->status != 200 )
    {
        return false;
    }
    std::istringstream iss( res->body );
    int a = 0;
    if ( !( iss >> Current >> Total >> a ) )
    {
        return false;
    }
    Active = ( a != 0 );
    return true;
}

//! split a single-pricer MCL request across the slaves and aggregate; anything
//! else (non-MCL, or a non-pricer root) is forwarded whole to the first slave.
static string ClusterPrice( const string& Body,
                            const string& ExecName,
                            const vector<string>& Slaves )
{
    YamlConfig req( YamlConfig::from_string_t{}, Body );
    const string exec = ( ExecName == ROOT_NODE ) ? req.GetString( "root" ) : ExecName;

    //! only an MCL single-pricer can be path-split
    bool splittable = req.GetTag( exec ) == "pricer";
    string cfg, mcl;
    if ( splittable )
    {
        cfg = req.GetString( exec + ".configuration" );
        splittable = req.GetString( cfg + ".method" ) == "mcl" &&
                     req.IsString( cfg + ".mcl_configuration" );
    }
    if ( !splittable )
    {
        //! nothing to distribute (non-MCL engine, a non-pricer root such as a
        //! !sequence or a historical analytic, or an MCL pricer with no path
        //! config) : the master computes it itself rather than offloading a whole,
        //! unsplit job onto one slave. Runs under the master's price_mutex, so it
        //! stays serialised with the rest of the cluster pricing.
        LOG( "CLU", "request is not a splittable MCL pricer : computing on the master" );
        return ExecuteYaml( Body, ExecName );
    }
    mcl = req.GetString( cfg + ".mcl_configuration" );

    const int N = (int)Slaves.size();
    const long total = req.GetInteger( mcl + ".paths" );
    const string result = req.GetString( exec + ".result" );
    const vector<string> contracts = req.GetStringList( req.GetString( exec + ".book" ) + ".options" );

    //! split paths (remainder to the first slave), one disjoint seed per slave
    vector<long> npaths( N, total / N );
    npaths[0] += total - ( total / N ) * N;

    vector<string> bodies( N );
    for ( int k = 0; k < N; k++ )
    {
        req.SetString( mcl + ".paths", std::to_string( npaths[k] ) );
        req.SetString( mcl + ".seed", std::to_string( k ) );
        bodies[k] = req.Dump();
    }

    //! dispatch concurrently
    LOG( "CLU", "dispatching " + std::to_string( total ) + " paths across " +
                    std::to_string( N ) + " slave(s)" );
    vector<string> responses( N );
    vector<std::exception_ptr> errs( N );
    vector<std::thread> threads;
    for ( int k = 0; k < N; k++ )
    {
        threads.emplace_back( [&, k]()
                              {
            try { responses[k] = PostToSlave( Slaves[k], bodies[k], ExecName ); }
            catch ( ... ) { errs[k] = std::current_exception(); } } );
    }

    //! approximate global progress : poll every slave's /progress and draw one
    //! aggregate bar (sum of paths done / sum of paths total, as a percent). It is
    //! "approximate" because slaves report at their own pace and a slow/old slave
    //! that fails to answer simply drops out of the running total for that tick.
    std::atomic<bool> dispatching{ true };
    std::thread poller( [&]()
                        {
        ProgressBar bar( "CLU", 100 ); //!< driven in whole percent
        while ( dispatching.load() )
        {
            long sum_cur = 0, sum_tot = 0;
            for ( const string& s : Slaves )
            {
                long c = 0, t = 0; bool a = false;
                if ( PollSlaveProgress( s, c, t, a ) ) { sum_cur += c; sum_tot += t; }
            }
            if ( sum_tot > 0 )
            {
                long pct = std::min<long>( 99, 100 * sum_cur / sum_tot ); //!< 100% left to Done()
                bar.Update( pct );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        }
        bar.Done(); } );

    for ( auto& t : threads )
    {
        t.join();
    }
    dispatching.store( false );
    poller.join();

    for ( int k = 0; k < N; k++ )
    {
        if ( errs[k] )
        {
            std::rethrow_exception( errs[k] );
        }
    }

    //! aggregate : path-weighted mean for values, combined variance for trusts
    const vector<string> gnames = { "delta", "gamma", "vega", "rho", "theta" };
    double premium = 0;
    double trust2 = 0;
    map<string, double> cprem, ctrust2, greeks;
    for ( int k = 0; k < N; k++ )
    {
        YamlConfig r( YamlConfig::from_string_t{}, responses[k] );
        const double w = (double)npaths[k] / (double)total;
        premium += w * r.GetDouble( result + ".premium" );
        const double tr = r.GetDouble( result + ".premium_trust", 0.0 );
        trust2 += w * w * tr * tr;
        for ( const string& c : contracts )
        {
            cprem[c] += w * r.GetDouble( result + "." + c + "_premium", 0.0 );
            const double ct = r.GetDouble( result + "." + c + "_premium_trust", 0.0 );
            ctrust2[c] += w * w * ct * ct;
        }
        for ( const string& g : gnames )
        {
            if ( r.IsDouble( result + "." + g ) )
            {
                greeks[g] += w * r.GetDouble( result + "." + g );
            }
        }
    }

    //! patch the first slave's response with the pooled numbers
    YamlConfig out( YamlConfig::from_string_t{}, responses[0] );
    out.SetDouble( result + ".premium", premium );
    out.SetDouble( result + ".premium_trust", sqrt( trust2 ) );
    for ( const string& c : contracts )
    {
        out.SetDouble( result + "." + c + "_premium", cprem[c] );
        out.SetDouble( result + "." + c + "_premium_trust", sqrt( ctrust2[c] ) );
    }
    for ( const auto& [g, v] : greeks )
    {
        out.SetDouble( result + "." + g, v );
    }
    out.SetString( "system_information.cluster",
                   "aggregated " + std::to_string( total ) + " paths over " +
                       std::to_string( N ) + " slaves" );
    return out.Dump();
}

//! cluster master : POST /price splits + dispatches to slaves, GET /health
int RunClusterMaster( int Port,
                      const vector<string>& Slaves )
{
    httplib::Server server;
    //! one cluster pricing at a time; the slave fan-out uses its own std::threads
    //! (below), so a small server pool is enough.
    server.new_task_queue = [] { return new httplib::ThreadPool( 4 ); };
    static std::mutex price_mutex; //!< one cluster pricing at a time

    server.Get( "/health", []( const httplib::Request&, httplib::Response& res )
                { res.set_content( "ok\n", "text/plain" ); } );

    server.Post( "/price", [Slaves]( const httplib::Request& req, httplib::Response& res )
                 {
        const string exec_name = req.has_header( "X-Exec-Name" )
                                 ? req.get_header_value( "X-Exec-Name" )
                                 : string( ROOT_NODE );
        const string client = req.remote_addr + ":" + std::to_string( req.remote_port );
        LOG( "CLU", "client " + client + " connected (price request)" );

        std::lock_guard<std::mutex> lock( price_mutex );
        string result;
        try { result = ClusterPrice( req.body, exec_name, Slaves ); }
        catch ( const std::exception& e ) { result = string( "error: " ) + e.what() + "\n"; }
        catch ( ... ) { result = "error: unknown cluster failure\n"; }
        res.set_content( result, "application/x-yaml" ); } );

    cout << "Thoth cluster master listening on 0.0.0.0:" << Port << " with " << Slaves.size()
         << " slave(s):" << endl;
    for ( const string& s : Slaves )
    {
        cout << "  - " << s << endl;
    }

    if ( !server.listen( "0.0.0.0", Port ) )
    {
        cerr << "error: could not bind port " << Port << endl;
        return 1;
    }
    return 0;
}

//! HTTP client : POST a YAML file to a thoth server and print the result
int RunClient( const string& Url,
               const string& InputFile )
{
    std::ifstream file( InputFile );
    if ( !file )
    {
        cerr << "error: cannot open " << InputFile << endl;
        return 1;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    httplib::Client client( Url );
    client.set_read_timeout( 3600 ); //!< pricing can take a while
    client.set_write_timeout( 60 );

    auto res = client.Post( "/price", buffer.str(), "application/x-yaml" );
    if ( !res )
    {
        cerr << "error: request to " << Url << " failed ("
             << httplib::to_string( res.error() ) << ")" << endl;
        return 1;
    }
    cout << res->body;
    //! the server streams errors as a body starting with "error: " (chunked
    //! responses are always 200), so detect failures from the payload too.
    bool failed = res->status != 200 || res->body.rfind( "error: ", 0 ) == 0;
    return failed ? 1 : 0;
}

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

//! parsing command line
RunningMode GetRunningMode( const int argc,
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
void DisplayHelp()
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
