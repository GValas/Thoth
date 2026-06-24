#pragma once

//! ----------------------------------------------------------------------
//! RAII ownership for the C-style linalg handles (la_vector / la_matrix).
//!
//! Those containers are created with la_*_alloc and released with la_*_free.
//! LaOwner wraps such a handle so the free happens automatically (no explicit
//! alloc/free in client code).  It converts implicitly to the raw pointer, so
//! the free-function calls and pointer member accesses keep working unchanged.
//! ----------------------------------------------------------------------

#include "linalg.hpp"

#include <utility>

//! ----------------------------------------------------------------------
//! Generic scope guard: runs a cleanup callable on scope exit — including when
//! the scope is left by an exception. Use it to restore mutated shared state
//! across a call that may throw (e.g. a bump-and-revalue reprice that ERRs), so
//! the restore is not skipped by the throw. Call dismiss() to cancel the cleanup.
//! ----------------------------------------------------------------------
template <class F>
class ScopeGuard
{
    F _cleanup;
    bool _active = true;

  public:
    explicit ScopeGuard( F cleanup ) : _cleanup( std::move( cleanup ) ) {}

    ~ScopeGuard()
    {
        if ( _active )
            _cleanup();
    }

    void dismiss() { _active = false; } //!< cancel the cleanup (commit the change)

    ScopeGuard( ScopeGuard&& o ) noexcept : _cleanup( std::move( o._cleanup ) ), _active( o._active )
    {
        o._active = false;
    }
    ScopeGuard( const ScopeGuard& ) = delete;
    ScopeGuard& operator=( const ScopeGuard& ) = delete;
    ScopeGuard& operator=( ScopeGuard&& ) = delete;
};

template <class T, void ( *Free )( T* )>
class LaOwner
{
    T* _p = nullptr;

  public:
    LaOwner() = default;
    LaOwner( T* p ) : _p( p ) {} //!< adopt a raw handle

    ~LaOwner()
    {
        if ( _p )
            Free( _p );
    }

    LaOwner( LaOwner&& o ) noexcept : _p( o._p ) { o._p = nullptr; }
    LaOwner& operator=( LaOwner&& o ) noexcept
    {
        if ( this != &o )
        {
            if ( _p )
                Free( _p );
            _p = o._p;
            o._p = nullptr;
        }
        return *this;
    }

    LaOwner& operator=( T* p ) //!< adopt a raw handle
    {
        if ( _p && _p != p )
            Free( _p );
        _p = p;
        return *this;
    }

    LaOwner( const LaOwner& ) = delete;
    LaOwner& operator=( const LaOwner& ) = delete;

    operator T*() const { return _p; } //!< implicit decay to raw
    T* operator->() const { return _p; }
    T* get() const { return _p; }
    explicit operator bool() const { return _p != nullptr; }

    //! relinquish ownership, returning the raw handle (caller must free it).
    //! [[nodiscard]]: dropping the returned handle leaks the resource.
    [[nodiscard]] T* release()
    {
        T* p = _p;
        _p = nullptr;
        return p;
    }
};

using LaVector = LaOwner<la_vector, la_vector_free>;
using LaMatrix = LaOwner<la_matrix, la_matrix_free>;
