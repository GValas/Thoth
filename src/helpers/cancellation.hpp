#pragma once

//! Cooperative cancellation for a single pricing run.
//!
//! Pricing is serialised (one request prices at a time, guarded by the server
//! mutex), so a single process-wide flag is enough. The HTTP server raises it
//! when it detects the client has disconnected; long-running loops poll
//! CancellationPoint() and abort by throwing.
namespace cancellation
{
void Reset();             //!< clear the flag before starting a run
void Request();           //!< raise the flag (called by the connection monitor)
bool Requested();         //!< non-throwing poll
void CancellationPoint(); //!< throw std::runtime_error if cancellation was requested
} // namespace cancellation
