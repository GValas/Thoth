//! ----------------------------------------------------------------------------
//! run_server.cpp : the `-server` run mode — the HTTP pricing daemon. Exposes
//! three endpoints over httplib:
//!   POST /price    : YAML book in -> YAML result out (the actual work)
//!   GET  /health   : liveness probe ("ok")
//!   GET  /progress : "<current> <total> <active>" for the in-flight pricing,
//!                    which the cluster master polls to draw a global bar
//! Pricing is intentionally serialised (the numeric core keeps process-global
//! state and is not re-entrant) and streamed back via a chunked provider so the
//! server can detect a client disconnect mid-pricing and cancel the work.
//! ----------------------------------------------------------------------------

#include "run_modes.hpp"
#include "cancellation.hpp"
#include "object_manager.hpp"
#include "progress_bar.hpp"

#include <atomic>
#include <chrono>
#include <httplib.h>
#include <mutex>
#include <sstream>
#include <thread>

//! execute an in-memory YAML request (any task, not only pricing) and return the
//! YAML result (throws on error). Shared with the cluster master (run_cluster.cpp).
//! Runs the same four-stage pipeline as RunBatch but entirely in memory (no files):
//! build the object graph for TaskName, execute, serialise, return as a string.
string ExecuteYaml( const string& YamlRequest, const string& TaskName )
{
    ObjectManager manager( YamlConfig::from_string_t{}, YamlRequest );
    manager.ReadObjects( TaskName );
    manager.ExecuteTask();
    manager.WriteResults();
    return manager.ResultYaml();
}

//! HTTP pricing server : POST /price (YAML in -> YAML out), GET /health,
//! GET /progress. Blocks in server.listen() until the process is killed or the
//! bind fails. Returns 1 on bind failure, otherwise does not return.
int RunHttpServer( int Port )
{
    httplib::Server server;

    //! pricing is serialised by price_mutex below, so a slave only ever handles
    //! one price at a time; cap the worker pool small (default is ~one thread per
    //! core) so a many-slave cluster on one box does not spawn hundreds of idle
    //! threads at startup (which made high slave counts fail to come up in time).
    server.new_task_queue = []
    { return new httplib::ThreadPool( 4 ); };

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
        //! optional X-Task-Name header selects the task; absent -> price the book root
        const string task_name = req.has_header( "X-Task-Name" )
                                 ? req.get_header_value( "X-Task-Name" )
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
            [body, task_name, client]( size_t /*offset*/, httplib::DataSink& sink ) -> bool
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

                //! price on a worker thread so this provider thread stays free to
                //! watch the socket; any error is turned into an "error: ..." body
                //! (the client/cluster detects failure from that prefix, since a
                //! chunked response status is always 200).
                string result;
                std::atomic<bool> done{ false };
                std::thread worker( [&]()
                {
                    try { result = ExecuteYaml( body, task_name ); }
                    catch ( const std::exception& e ) { result = string( "error: " ) + e.what() + "\n"; }
                    catch ( ... ) { result = "error: unknown failure\n"; }
                    done.store( true ); //!< signal the poll loop below that pricing finished
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
