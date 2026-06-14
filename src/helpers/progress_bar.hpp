#pragma once
#include "thoth.hpp"
#include <functional>

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
    ProgressBar( const string& Label, long Total, bool Enabled = true );

    void Update( long Current );
    //! lazy variant : InfoFn is only evaluated when the bar actually redraws
    void Update( long Current, const std::function<string()>& InfoFn );

    void Done();
    void Done( const string& Info );

  private:
    void Render( long Current, const string& Info, bool Final );

    string _label;
    long _total;
    bool _enabled;
    bool _tty;
    time_t _start;
    int _last_percent;  //!< last drawn percent (throttles redraws to 1% steps)
    size_t _last_width; //!< displayed columns of the last TTY line, to clear leftovers
};
