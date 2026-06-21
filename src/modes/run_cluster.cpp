#include "run_modes.hpp"
#include "object_manager.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <httplib.h>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include "progress_bar.hpp"

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
                           const string& TaskName )
{
    httplib::Client client( Url );
    client.set_read_timeout( 3600 );
    client.set_write_timeout( 60 );

    httplib::Headers headers;
    if ( TaskName != ROOT_NODE )
    {
        headers.emplace( "X-Task-Name", TaskName );
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

//! forward declaration : a !sequence is dispatched per sub-task (mutual recursion
//! with ClusterPrice, which a sub-task re-enters as an individual pricer).
static string ClusterPriceSequence( const string& Body,
                                    const string& TaskName,
                                    const vector<string>& Slaves );

//! split a single-pricer MCL request across the slaves and aggregate. A !sequence
//! root is dispatched task by task (each MCL cell path-split in turn); anything
//! else (non-MCL engine, an MCL pricer with no path config) is computed on the
//! master rather than offloading a whole, unsplit job onto one slave.
static string ClusterPrice( const string& Body,
                            const string& TaskName,
                            const vector<string>& Slaves )
{
    YamlConfig req( YamlConfig::from_string_t{}, Body );
    const string task = ( TaskName == ROOT_NODE ) ? req.GetString( "root" ) : TaskName;

    //! a sequence (e.g. the full pricer matrix) : apply the cluster logic to each
    //! sub-pricer in turn, so its MCL cells are path-split across the slaves too.
    if ( req.GetTag( task ) == "sequence" )
    {
        return ClusterPriceSequence( Body, task, Slaves );
    }

    //! only a plain (CPU) MCL single-pricer is path-split across the slaves
    bool splittable = req.GetTag( task ) == "pricer";
    string cfg, mcl;
    bool allow_gpu = false;
    if ( splittable )
    {
        cfg = req.GetString( task + ".configuration" );
        if ( req.GetString( cfg + ".method" ) == "mcl" && req.IsString( cfg + ".mcl_configuration" ) )
        {
            mcl = req.GetString( cfg + ".mcl_configuration" );
            //! a GPU cell (allow_gpu) is already massively data-parallel on one
            //! device, so it is NOT split — the master prices it whole on its own
            //! GPU rather than fanning a fraction of the paths out to each slave.
            allow_gpu = req.GetBoolean( mcl + ".allow_gpu", false );
            splittable = !allow_gpu;
        }
        else
        {
            splittable = false;
        }
    }
    if ( !splittable )
    {
        //! computed on the master rather than offloading a whole, unsplit job onto
        //! one slave (non-MCL engine, an MCL pricer with no path config, or a GPU
        //! cell which runs on the master's device). Under the master's price_mutex,
        //! so it stays serialised with the rest of the cluster pricing.
        LOG( allow_gpu ? "GPU" : "CLU",
             allow_gpu ? "GPU pricer : computing on the master device (not path-split)"
                       : "request is not a splittable MCL pricer : computing on the master" );
        return ExecuteYaml( Body, TaskName );
    }

    const long total = req.GetLong( mcl + ".paths" );
    const string result = req.GetString( task + ".result" );

    //! slaves actually used: never hand a slave 0 paths (a zero-path book is
    //! rejected with "paths must be > 0"), so cap the fan-out at `total`. A
    //! non-positive total falls through to the slaves' own validation error.
    const int N = ( total > 0 && total < (long)Slaves.size() ) ? (int)total : (int)Slaves.size();

    //! balanced split: the first (total % N) slaves take one extra path. Each slave
    //! gets an explicit Sobol skip = the running path count of the slaves before it,
    //! so the per-slave blocks stay strictly disjoint even when the split is uneven.
    vector<long> npaths( N, total / N );
    for ( long i = 0; i < total % (long)N; i++ )
    {
        npaths[i] += 1;
    }
    vector<long> skip( N, 0 );
    for ( int k = 1; k < N; k++ )
    {
        skip[k] = skip[k - 1] + npaths[k - 1];
    }

    vector<string> bodies( N );
    for ( int k = 0; k < N; k++ )
    {
        req.Set( mcl + ".paths", std::to_string( npaths[k] ) );
        req.Set( mcl + ".seed", std::to_string( k ) );
        req.Set( mcl + ".sobol_skip", std::to_string( skip[k] ) );
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
            try { responses[k] = PostToSlave( Slaves[k], bodies[k], TaskName ); }
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
            for ( int k = 0; k < N; k++ )
            {
                long c = 0, t = 0; bool a = false;
                if ( PollSlaveProgress( Slaves[k], c, t, a ) ) { sum_cur += c; sum_tot += t; }
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

    //! aggregate every numeric field a slave reported, schema-agnostically. The
    //! result block holds premium, the requested book Greeks (delta..theta), the
    //! model-parameter Greeks (vega_<param>, e.g. vega_v0/vega_kappa) and the
    //! per-contract premia/Greeks — all pooled the same way. Enumerating the keys
    //! (rather than matching a hardcoded list by eye) is what keeps producer and
    //! aggregator in sync: a fixed {delta..theta} list silently dropped every
    //! vega_<param> on the cluster. Path-weighted mean for values; quadrature
    //! (sum of w^2*stderr^2) for the *_trust standard errors. task_time is a
    //! per-slave wall time (not poolable) and string fields (kind, *_graph) are
    //! left as the first slave's copy.
    auto ends_with = []( const string& s, const string& suf )
    { return s.size() >= suf.size() && s.compare( s.size() - suf.size(), suf.size(), suf ) == 0; };

    map<string, double> pooled; //!< path-weighted means (premium, Greeks, ...)
    map<string, double> trust2; //!< summed w^2 * stderr^2, square-rooted on write
    const vector<string> keys =
        YamlConfig( YamlConfig::from_string_t{}, responses[0] ).GetChildKeys( result );
    for ( int k = 0; k < N; k++ )
    {
        YamlConfig r( YamlConfig::from_string_t{}, responses[k] );
        const double w = (double)npaths[k] / (double)total;
        for ( const string& key : keys )
        {
            const string path = result + "." + key;
            if ( key == "task_time" || !r.IsDouble( path ) )
            {
                continue;
            }
            const double v = r.GetDouble( path, 0.0 );
            if ( ends_with( key, "_trust" ) )
            {
                trust2[key] += w * w * v * v;
            }
            else
            {
                pooled[key] += w * v;
            }
        }
    }

    //! patch the first slave's response with the pooled numbers
    YamlConfig out( YamlConfig::from_string_t{}, responses[0] );
    for ( const auto& [key, v] : pooled )
    {
        out.Set( result + "." + key, v );
    }
    for ( const auto& [key, v] : trust2 )
    {
        out.Set( result + "." + key, sqrt( v ) );
    }
    out.Set( "system_information.cluster",
             "aggregated " + std::to_string( total ) + " paths over " +
                 std::to_string( N ) + " slaves" );
    return out.Dump();
}

//! price a !sequence under the cluster master : run every sub-task through the
//! per-pricer dispatch (each MCL cell path-split across the slaves in turn, ANA /
//! PDE / mcl_gpu computed on the master), then gather every sub-task's result
//! block into one response — so the matrix gets the slaves applied cell by cell.
//! Sub-tasks run one after another (not concurrently): each MCL cell already uses
//! the whole slave pool, and serial execution keeps each cell's aggregate progress
//! bar coherent. Each call re-prices from the original Body, so sub-pricers sharing
//! a book stay independent (no carried-over state between cells).
static string ClusterPriceSequence( const string& Body,
                                    const string& TaskName,
                                    const vector<string>& Slaves )
{
    YamlConfig out( YamlConfig::from_string_t{}, Body ); //!< accumulator = input doc
    const vector<string> tasks = out.GetStringList( TaskName + ".tasks" );
    LOG( "SEQ", "sequence '" + TaskName + "' : dispatching " + std::to_string( tasks.size() ) + " task(s) across the cluster" );

    const double t0 = WallClockSeconds();
    for ( size_t i = 0; i < tasks.size(); i++ )
    {
        LOG( "SEQ", "task " + std::to_string( i + 1 ) + "/" + std::to_string( tasks.size() ) + " : " + tasks[i] );
        const string resp = ClusterPrice( Body, tasks[i], Slaves );
        YamlConfig r( YamlConfig::from_string_t{}, resp );
        out.CopyTopLevel( out.GetString( tasks[i] + ".result" ), r );
    }

    //! final SEQ line with the total time, mirroring the in-process Sequence::Execute
    const double total_time = TaskTime( t0 );
    LOG( "SEQ", "ran " + std::to_string( tasks.size() ) + " task(s), task_time = " +
                    ToString( total_time ) + " sec" );

    //! sequence summary block, mirroring Sequence::WriteResults
    const string seq_result = out.GetString( TaskName + ".result", "" );
    if ( !seq_result.empty() )
    {
        out.Set( seq_result + ".kind", "sequence_result" );
        out.Set( seq_result + ".task_time", total_time );
        out.Set( seq_result + ".tasks", tasks );
    }
    out.Set( "system_information.cluster",
             "sequence of " + std::to_string( tasks.size() ) +
                 " task(s) dispatched over " + std::to_string( Slaves.size() ) + " slave(s)" );
    return out.Dump();
}

//! cluster master : POST /price splits + dispatches to slaves, GET /health
int RunClusterMaster( int Port,
                      const vector<string>& Slaves )
{
    httplib::Server server;
    //! one cluster pricing at a time; the slave fan-out uses its own std::threads
    //! (below), so a small server pool is enough.
    server.new_task_queue = []
    { return new httplib::ThreadPool( 4 ); };
    static std::mutex price_mutex; //!< one cluster pricing at a time

    server.Get( "/health", []( const httplib::Request&, httplib::Response& res )
                { res.set_content( "ok\n", "text/plain" ); } );

    server.Post( "/price", [Slaves]( const httplib::Request& req, httplib::Response& res )
                 {
        const string task_name = req.has_header( "X-Task-Name" )
                                 ? req.get_header_value( "X-Task-Name" )
                                 : string( ROOT_NODE );
        const string client = req.remote_addr + ":" + std::to_string( req.remote_port );
        LOG( "CLU", "client " + client + " connected (price request)" );

        std::lock_guard<std::mutex> lock( price_mutex );
        string result;
        try { result = ClusterPrice( req.body, task_name, Slaves ); }
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
