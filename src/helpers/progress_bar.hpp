#pragma once
#include "thoth.hpp"
#include <atomic>
#include <functional>

//! Process-wide snapshot of the most recently active (enabled) progress bar, so
//! an HTTP server can report the in-flight pricing's advancement (GET /progress)
//! to a cluster master without coupling to the pricer internals. Pricings are
//! serialised on a server, so at most one enabled bar is live at a time.
struct GlobalProgress
{
    std::atomic<long> current{ 0 };
    std::atomic<long> total{ 0 };
    std::atomic<bool> active{ false }; //!< true while a pricing is running
};
GlobalProgress& global_progress();

//! Single-line progress bar for long pricing loops.
//!
//! On a terminal it redraws in place with '\r' a single short line, hard-capped
//! to the terminal width so it never wraps (a wrapped line would break the
//! in-place redraw into one row per update). When stdout is not a TTY (output
//! redirected to a file, or captured e.g. via `docker logs`) it falls back to
//! one newline-terminated bar line every 10% so logs stay readable instead of
//! piling up the redraw frames. Construct with a label and total count, call
//! Update() as work advances, and Done() when finished.
class ProgressBar
{
  public:
    //! Enabled=false makes the bar a no-op (Update/Done print nothing). Used to
    //! silence the inner re-prices of bump-and-revalue Greeks, which would
    //! otherwise redraw the bar once per bumped scenario.
    //! PublishGlobal=false keeps this bar's advancement out of the process-wide
    //! GlobalProgress snapshot (GET /progress). Used for a slave-local pass (e.g.
    //! the theta one-day reprice) that should still display locally but must not
    //! disturb the cluster master's aggregate progress, which tracks the split.
    ProgressBar( const string& Label, long Total, bool Enabled = true, bool PublishGlobal = true );

    //! idle bar: a no-op until Start() configures it. Lets a Task own one as a member
    //! (constructed with the task) and (re)begin it per execution phase.
    ProgressBar() = default;

    //! (re)begin a run: set the label / total / visibility and reset the render state.
    //! The same bar object can be Started for several successive phases.
    void Start( const string& Label, long Total, bool Enabled = true, bool PublishGlobal = true );

    void Update( long Current );
    //! lazy variant : InfoFn is only evaluated when the bar actually redraws
    void Update( long Current, const std::function<string()>& InfoFn );

    void Done();
    void Done( const string& Info );

  private:
    void Render( long Current, const string& Info, bool Final );

    string _label;
    long _total = 1;
    bool _enabled = false; //!< idle until Start(); Update/Done no-op meanwhile
    bool _publish_global = false;
    bool _tty = false;
    time_t _start = 0;
    int _last_percent = -1; //!< last drawn percent (throttles redraws to 1% steps)
    size_t _last_width = 0; //!< displayed columns of the last TTY line, to clear leftovers
};
