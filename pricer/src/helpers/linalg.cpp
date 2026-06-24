#include "linalg.hpp"

#include <algorithm>

//! --- vector ---

la_vector* la_vector_alloc( std::size_t n )
{
    auto* v = new la_vector();
    v->store.assign( n, 0.0 );
    v->size = n;
    v->data = v->store.data();
    return v;
}

la_vector* la_vector_calloc( std::size_t n )
{
    return la_vector_alloc( n ); //!< store is already zero-initialised
}

void la_vector_free( la_vector* v )
{
    delete v;
}

double la_vector_get( const la_vector* v, std::size_t i )
{
    return v->data[i];
}

void la_vector_set( la_vector* v, std::size_t i, double x )
{
    v->data[i] = x;
}

double* la_vector_ptr( la_vector* v, std::size_t i )
{
    return v->data + i;
}

void la_vector_memcpy( la_vector* dst, const la_vector* src )
{
    std::copy( src->store.begin(), src->store.end(), dst->store.begin() );
}

void la_vector_scale( la_vector* v, double c )
{
    for ( double& x : v->store )
    {
        x *= c;
    }
}

//! --- matrix (row-major) ---

la_matrix* la_matrix_alloc( std::size_t rows, std::size_t cols )
{
    auto* m = new la_matrix();
    m->store.assign( rows * cols, 0.0 );
    m->size1 = rows;
    m->size2 = cols;
    m->data = m->store.data();
    return m;
}

la_matrix* la_matrix_calloc( std::size_t rows, std::size_t cols )
{
    return la_matrix_alloc( rows, cols ); //!< store is already zero-initialised
}

void la_matrix_free( la_matrix* m )
{
    delete m;
}

double la_matrix_get( const la_matrix* m, std::size_t i, std::size_t j )
{
    return m->data[i * m->size2 + j];
}

void la_matrix_set( la_matrix* m, std::size_t i, std::size_t j, double x )
{
    m->data[i * m->size2 + j] = x;
}

void la_matrix_memcpy( la_matrix* dst, const la_matrix* src )
{
    std::copy( src->store.begin(), src->store.end(), dst->store.begin() );
}
