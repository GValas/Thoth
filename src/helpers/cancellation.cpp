#include "cancellation.hpp"

#include <atomic>
#include <stdexcept>

namespace
{
//! process-wide cancellation flag. relaxed ordering is sufficient: this is a lone
//! flag with no other memory it must synchronise against — the worker only needs
//! to observe the latest value eventually, not establish a happens-before edge.
std::atomic<bool> g_cancel{ false };
} // namespace

namespace cancellation
{
void Reset()
{
    g_cancel.store( false, std::memory_order_relaxed );
}

void Request()
{
    g_cancel.store( true, std::memory_order_relaxed );
}

bool Requested()
{
    return g_cancel.load( std::memory_order_relaxed );
}

void CancellationPoint()
{
    if ( g_cancel.load( std::memory_order_relaxed ) )
    {
        throw std::runtime_error( "pricing cancelled (client disconnected)" );
    }
}
} // namespace cancellation
