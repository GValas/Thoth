#include "cancellation.hpp"

#include <atomic>
#include <stdexcept>

namespace
{
std::atomic<bool> g_cancel{ false };
}

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
