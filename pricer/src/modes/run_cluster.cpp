#include "run_modes.hpp"
#include "result_schema.hpp" //!< canonical result field names (shared with the producer)
#include "object_manager.hpp"
#include "sequence.hpp" //!< WriteSequenceSummary — shared sequence-summary schema

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

//! split a single ANA/PDE pricer's BOOK across the slaves by contract: each slave prices
//! a disjoint subset of the contracts and the results are reassembled. This is exact for
//! these engines because every contract is priced independently (no cross-contract
//! coupling; the shared market data + correlation are replicated to each slave), so the
//! per-contract fields are identical to a single-box run and the book-level aggregates are
//! just their sum. Per-contract fields (<contract>_*) are disjoint across slaves and so
//! unioned; book-level fields (premium, premium_trust, the book Greeks and any model-param
//! vega_<x>) are summed (premium_trust in quadrature); task_time becomes the slowest slave
//! (the work runs in parallel). Caller guarantees >= 2 contracts and >= 2 slaves.
static string ClusterPriceByContract( const string& Body,
                                      const string& task,
                                      const string& TaskName,
                                      const vector<string>& Slaves )
{
    YamlConfig req( YamlConfig::from_string_t{}, Body );
    const string book = req.GetString( task + ".book" );
    const vector<string> contracts = req.GetStringList( book + ".contracts" );
    const string result = req.GetString( task + ".result" );

    const int N = std::min( (int)Slaves.size(), (int)contracts.size() );

    //! round-robin partition: cells are emitted in (strike, maturity) order with maturity
    //! innermost, so dealing them out cyclically spreads the costlier long maturities evenly
    //! across slaves rather than piling them onto the last chunk.
    vector<vector<string>> chunks( N );
    for ( size_t i = 0; i < contracts.size(); i++ )
    {
        chunks[i % N].push_back( contracts[i] );
    }

    //! one body per slave: the same document with the book's contract list reduced to the
    //! chunk (mutate `req` in place and snapshot via Dump, as the MCL path-split does).
    vector<string> bodies( N );
    for ( int k = 0; k < N; k++ )
    {
        req.Set( book + ".contracts", chunks[k] );
        bodies[k] = req.Dump();
    }

    LOG( "CLU", "splitting " + std::to_string( contracts.size() ) + " contracts across " +
                    std::to_string( N ) + " slave(s)" );

    //! dispatch concurrently : one thread per slave, errors captured per index (same pattern
    //! and progress aggregation as the MCL path-split).
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

    std::atomic<bool> dispatching{ true };
    std::thread poller( [&]()
                        {
        ProgressBar bar( "CLU", 100 );
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
                bar.Update( std::min<long>( 99, 100 * sum_cur / sum_tot ) );
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

    //! a key is per-contract iff it is "<contract>_<metric>" for one of the book's
    //! contracts; the trailing underscore keeps "cell_..._1" from matching "cell_..._10".
    auto is_per_contract = [&]( const string& key )
    {
        for ( const string& c : contracts )
        {
            if ( key.size() > c.size() + 1 && key.rfind( c + "_", 0 ) == 0 )
            {
                return true;
            }
        }
        return false;
    };

    //! base = first slave's full document; overwrite book-level fields with the cross-slave
    //! sums and fold in the other slaves' (disjoint) per-contract fields.
    YamlConfig out( YamlConfig::from_string_t{}, responses[0] );
    map<string, double> book_sum; //!< summed book-level means (premium + Greeks)
    double trust2 = 0.0;          //!< book premium_trust combined in quadrature
    double max_time = 0.0;        //!< parallel wall time = slowest slave

    for ( int k = 0; k < N; k++ )
    {
        YamlConfig r( YamlConfig::from_string_t{}, responses[k] );
        for ( const string& key : r.GetChildKeys( result ) )
        {
            const string path = result + "." + key;
            if ( key == result_schema::TASK_TIME )
            {
                if ( r.IsDouble( path ) )
                {
                    max_time = std::max( max_time, r.GetDouble( path, 0.0 ) );
                }
                continue;
            }
            if ( !r.IsDouble( path ) )
            {
                continue; //!< strings (kind, *_graph) : keep the base slave's copy
            }
            const double v = r.GetDouble( path, 0.0 );
            if ( is_per_contract( key ) )
            {
                if ( k != 0 )
                {
                    out.Set( path, v ); //!< slave 0's per-contract fields are already in `out`
                }
            }
            else if ( key == result_schema::PREMIUM_TRUST )
            {
                trust2 += v * v;
            }
            else
            {
                book_sum[key] += v; //!< book-level aggregate -> sum
            }
        }
    }

    for ( const auto& [key, v] : book_sum )
    {
        out.Set( result + "." + key, v );
    }
    out.Set( result + "." + result_schema::PREMIUM_TRUST, sqrt( trust2 ) );
    out.Set( result + "." + result_schema::TASK_TIME, max_time );
    out.Set( "system_information.cluster",
             "book of " + std::to_string( contracts.size() ) + " contract(s) split over " +
                 std::to_string( N ) + " slave(s)" );
    return out.Dump();
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
    if ( req.GetTag( task ) == KIND_SEQUENCE )
    {
        return ClusterPriceSequence( Body, task, Slaves );
    }

    //! only a plain (CPU) MCL single-pricer is path-split across the slaves. The
    //! engine is the tag itself now (!mcl_pricer), so the split test is a tag check.
    bool splittable = req.GetTag( task ) == KIND_MCL_PRICER;
    string mcl;
    bool allow_gpu = false;
    if ( splittable )
    {
        if ( req.IsString( task + ".mcl_configuration" ) )
        {
            mcl = req.GetString( task + ".mcl_configuration" );
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
        //! ANA / PDE : the per-contract solves are independent, so split the book across
        //! the slaves by contract (each prices a disjoint subset) instead of running the
        //! whole book single-threaded on the master. Worth it only with something to
        //! parallelise (>= 2 contracts AND >= 2 slaves); GPU-MCL stays on the master since
        //! it uses the master's device, not the CPU slaves.
        const string tag = req.GetTag( task );
        if ( ( tag == KIND_ANA_PRICER || tag == KIND_PDE_PRICER ) && Slaves.size() >= 2 )
        {
            const string book = req.GetString( task + ".book", "" );
            if ( !book.empty() && req.IsStringList( book + ".contracts" ) &&
                 req.GetStringList( book + ".contracts" ).size() >= 2 )
            {
                return ClusterPriceByContract( Body, task, TaskName, Slaves );
            }
        }

        //! computed on the master rather than offloading a whole, unsplit job onto
        //! one slave (a GPU cell on the master's device, or an ANA/PDE book too small
        //! to split). Under the master's price_mutex, so it stays serialised with the
        //! rest of the cluster pricing.
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

    //! build one request body per slave by mutating the shared `req` in place and
    //! dumping a snapshot each iteration. Per slave: its path count, a distinct
    //! seed (k) for pseudo-random streams, and a Sobol skip so the quasi-random
    //! sub-sequences are disjoint and the union reproduces the single-box run.
    vector<string> bodies( N );
    for ( int k = 0; k < N; k++ )
    {
        req.Set( mcl + ".paths", std::to_string( npaths[k] ) );
        req.Set( mcl + ".seed", std::to_string( k ) );
        req.Set( mcl + ".sobol_skip", std::to_string( skip[k] ) );
        bodies[k] = req.Dump(); //!< snapshot now; the next iteration overwrites req
    }

    //! dispatch concurrently : one thread per slave POSTs its body and stores the
    //! response (or its exception) into a per-index slot, so the threads never
    //! touch the same element and need no locking. Errors are rethrown after join.
    LOG( "CLU", "dispatching " + std::to_string( total ) + " paths across " +
                    std::to_string( N ) + " slave(s)" );
    vector<string> responses( N );
    vector<std::exception_ptr> errs( N );
    vector<std::thread> threads;
    for ( int k = 0; k < N; k++ )
    {
        threads.emplace_back( [&, k]() //!< capture k by value : each thread owns its index
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
    map<string, double> pooled; //!< path-weighted means (premium, Greeks, ...)
    map<string, double> trust2; //!< summed w^2 * stderr^2, square-rooted on write
    const vector<string> keys =
        YamlConfig( YamlConfig::from_string_t{}, responses[0] ).GetChildKeys( result );
    for ( int k = 0; k < N; k++ )
    {
        YamlConfig r( YamlConfig::from_string_t{}, responses[k] );
        const double w = (double)npaths[k] / (double)total; //!< this slave's path-share weight
        for ( const string& key : keys )
        {
            const string path = result + "." + key;
            if ( key == result_schema::TASK_TIME || !r.IsDouble( path ) )
            {
                continue;
            }
            const double v = r.GetDouble( path, 0.0 );
            if ( result_schema::IsTrust( key ) )
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

    //! sequence summary block — written through the same helper as the in-process
    //! Sequence::WriteResults so the cluster output cannot drift from it
    const string seq_result = out.GetString( TaskName + ".result", "" );
    if ( !seq_result.empty() )
    {
        WriteSequenceSummary( out, seq_result, total_time, tasks );
    }
    out.Set( "system_information.cluster",
             "sequence of " + std::to_string( tasks.size() ) +
                 " task(s) dispatched over " + std::to_string( Slaves.size() ) + " slave(s)" );
    return out.Dump();
}

//! cluster master : POST /price splits + dispatches to slaves, GET /health.
//! Blocks in server.listen() until killed or a bind failure (returns 1 then).
//! Unlike the plain server it does NOT stream/cancel : a cluster price holds
//! price_mutex and runs to completion, since the heavy work is on the slaves.
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

    //! progress of the in-flight cluster pricing : "<current> <total> <active 0|1>",
    //! same contract as the plain server so a BFF (or any poller) leasing the master
    //! reads it uniformly. Reads only atomics (no price_mutex), so it answers while a
    //! price runs. It is populated whether the master path-splits (the aggregate "CLU"
    //! bar publishes the global percent) or computes on its own device (ANA / PDE /
    //! mcl_gpu — the inner pricer's bar publishes directly). Without it the master 404s
    //! every poll, which the BFF surfaces as a failed grid even though the price ran.
    server.Get( "/progress", []( const httplib::Request&, httplib::Response& res )
                {
        const GlobalProgress& g = global_progress();
        std::ostringstream oss;
        oss << g.current.load() << " " << g.total.load() << " " << ( g.active.load() ? 1 : 0 );
        res.set_content( oss.str(), "text/plain" ); } );

    server.Post( "/price", [Slaves]( const httplib::Request& req, httplib::Response& res )
                 {
        //! same X-Task-Name convention as the plain server (absent -> book root)
        const string task_name = req.has_header( "X-Task-Name" )
                                 ? req.get_header_value( "X-Task-Name" )
                                 : string( ROOT_NODE );
        const string client = req.remote_addr + ":" + std::to_string( req.remote_port );
        LOG( "CLU", "client " + client + " connected (price request)" );

        //! serialise : one cluster pricing at a time (the slave fan-out already
        //! uses the whole pool, and the master's own on-device pricing is non-reentrant)
        std::lock_guard<std::mutex> lock( price_mutex );
        string result;
        //! failures (slave unreachable, slave error, split logic) become an
        //! "error: ..." body — the same prefix convention the client detects.
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
