#pragma once

//! ----------------------------------------------------------------------
//! RAII ownership for GSL C handles.
//!
//! GSL exposes a C API whose objects are created with gsl_*_alloc and must
//! be released with gsl_*_free.  GslOwner wraps such a handle so the free
//! happens automatically (no explicit alloc/free in client code).  It
//! converts implicitly to the raw pointer, so existing gsl_* calls and
//! pointer member accesses keep working unchanged.
//! ----------------------------------------------------------------------

#include "linalg.hpp"

template <class T, void ( *Free )( T* )>
class GslOwner
{
    T* _p = nullptr;

  public:
    GslOwner() = default;
    GslOwner( T* p ) : _p( p ) {} //!< adopt a raw handle

    ~GslOwner()
    {
        if ( _p )
            Free( _p );
    }

    GslOwner( GslOwner&& o ) noexcept : _p( o._p ) { o._p = nullptr; }
    GslOwner& operator=( GslOwner&& o ) noexcept
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

    GslOwner& operator=( T* p ) //!< adopt a raw handle
    {
        if ( _p && _p != p )
            Free( _p );
        _p = p;
        return *this;
    }

    GslOwner( const GslOwner& ) = delete;
    GslOwner& operator=( const GslOwner& ) = delete;

    operator T*() const { return _p; } //!< implicit decay to raw
    T* operator->() const { return _p; }
    T* get() const { return _p; }
    explicit operator bool() const { return _p != nullptr; }

    //! relinquish ownership, returning the raw handle (caller must free it)
    T* release()
    {
        T* p = _p;
        _p = nullptr;
        return p;
    }
};

using GslVector = GslOwner<la_vector, la_vector_free>;
using GslMatrix = GslOwner<la_matrix, la_matrix_free>;
