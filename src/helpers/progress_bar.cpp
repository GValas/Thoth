#include "progress_bar.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
//! visible columns of the controlling terminal (fallback 80 when unknown)
int TerminalWidth()
{
    struct winsize ws;
    if ( ioctl( STDOUT_FILENO, TIOCGWINSZ, &ws ) == 0 && ws.ws_col > 0 )
    {
        return ws.ws_col;
    }
    return 80;
}

constexpr int TTY_BLOCKS = 10; //!< short bar so the in-place line fits one row
constexpr int LOG_BLOCKS = 30; //!< wider bar for the (newline) log fallback
} // namespace

GlobalProgress& global_progress()
{
    static GlobalProgress g;
    return g;
}

ProgressBar::ProgressBar( const string& Label, long Total, bool Enabled )
    : _label( Label ),
      _total( Total > 0 ? Total : 1 ),
      _enabled( Enabled ),
      _tty( isatty( STDOUT_FILENO ) != 0 ),
      _start( time( nullptr ) ),
      _last_percent( -1 ),
      _last_width( 0 )
{
    //! only the visible (enabled) bar publishes globally; the silenced inner
    //! re-price bars of bump-and-revalue Greeks must not clobber the snapshot.
    if ( _enabled )
    {
        GlobalProgress& g = global_progress();
        g.total.store( _total );
        g.current.store( 0 );
        g.active.store( true );
    }
}

void ProgressBar::Update( long Current )
{
    Update( Current, nullptr );
}

void ProgressBar::Update( long Current, const std::function<string()>& InfoFn )
{
    if ( !_enabled )
    {
        return;
    }
    global_progress().current.store( Current );
    int percent = (int)( 100.0 * Current / _total );
    if ( percent == _last_percent || percent >= 100 ) //!< nothing new; leave 100% to Done()
    {
        return;
    }
    _last_percent = percent;
    Render( Current, InfoFn ? InfoFn() : string(), false );
}

void ProgressBar::Done()
{
    Done( string() );
}

void ProgressBar::Done( const string& Info )
{
    if ( !_enabled )
    {
        return;
    }
    GlobalProgress& g = global_progress();
    g.current.store( _total );
    g.active.store( false ); //!< pricing finished; master poller can stop counting it
    Render( _total, Info, true );
}

void ProgressBar::Render( long Current, const string& Info, bool Final )
{
    int percent = (int)( 100.0 * Current / _total );

    if ( _tty )
    {
        //! a short, single line redrawn in place with '\r'. No timestamp, a
        //! 10-block bar, and the whole line is hard-capped to the terminal width
        //! so it never wraps (a wrapped line breaks the in-place redraw, leaving
        //! one row per update instead of a single updating line).
        int filled = percent * TTY_BLOCKS / 100;
        std::ostringstream oss;
        oss << LogTimestamp() << " " << _label << "> │"; //!< same timestamp prefix as LOG
        for ( int k = 0; k < TTY_BLOCKS; k++ )
        {
            oss << ( k < filled ? "█" : "░" );
        }
        oss << "│ " << percent << "%  " << Current << "/" << _total;
        if ( !Info.empty() )
        {
            oss << "  " << Info;
        }

        //! cap to the terminal width. The bar's TTY_BLOCKS box-drawing chars are
        //! 3 bytes / 1 column each, so allow 2 extra bytes per block; the cut
        //! lands in the ASCII tail (counts / info), so trimming bytes is safe.
        string body = oss.str();
        const size_t extra = (size_t)TTY_BLOCKS * 2;
        size_t max_bytes = (size_t)( TerminalWidth() - 1 ) + extra;
        if ( body.size() > max_bytes )
        {
            body.resize( max_bytes );
        }
        size_t width = body.size() - extra; //!< displayed columns

        string line = "\r" + body;
        if ( width < _last_width ) //!< clear leftovers from a previous longer line
        {
            line += string( _last_width - width, ' ' );
        }
        _last_width = width;
        cout << line << std::flush;
        if ( Final )
        {
            cout << endl;
        }
    }
    else //!< not a TTY : one newline-terminated bar line every 10% (and at 100%)
    {
        if ( Final || ( percent > 0 && percent % 10 == 0 ) )
        {
            double elapsed = difftime( time( nullptr ), _start );
            int filled = percent * LOG_BLOCKS / 100;
            std::ostringstream oss;
            oss << "│";
            for ( int k = 0; k < LOG_BLOCKS; k++ )
            {
                oss << ( k < filled ? "█" : "░" );
            }
            oss << "│ " << percent << "%  " << Current << "/" << _total << "  " << (long)elapsed << "s";
            if ( !Final && Current > 0 )
            {
                oss << " eta " << (long)( elapsed * ( _total - Current ) / Current ) << "s";
            }
            if ( !Info.empty() )
            {
                oss << "  " << Info;
            }
            LOG( _label, oss.str() );
        }
    }
}
