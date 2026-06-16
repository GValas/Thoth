#pragma once

#include <cstddef>
#include <vector>

//! ----------------------------------------------------------------------
//! Lightweight dense vector / matrix — the in-repo replacement for the GSL
//! containers (la_vector / la_matrix), final step of dropping the GPL GSL
//! dependency. Only the subset of the GSL C API the engine actually used is
//! provided, under the la_ prefix and with the same heap-alloc/free +
//! free-function style, so the migration is a one-to-one token rename. Storage
//! is row-major; objects are always used through pointers (never value-copied),
//! so copying is disabled to keep the cached `data` pointer valid.
//! ----------------------------------------------------------------------

struct la_vector
{
    std::size_t size = 0;
    double* data = nullptr; //!< points into store (stable: never resized)
    std::vector<double> store;

    la_vector() = default;
    la_vector( const la_vector& ) = delete;
    la_vector& operator=( const la_vector& ) = delete;
};

struct la_matrix
{
    std::size_t size1 = 0; //!< rows
    std::size_t size2 = 0; //!< cols
    double* data = nullptr;
    std::vector<double> store;

    la_matrix() = default;
    la_matrix( const la_matrix& ) = delete;
    la_matrix& operator=( const la_matrix& ) = delete;
};

//! --- vector ---
la_vector* la_vector_alloc( std::size_t n );  //!< zero-initialised
la_vector* la_vector_calloc( std::size_t n ); //!< zero-initialised (alias of alloc)
void la_vector_free( la_vector* v );
double la_vector_get( const la_vector* v, std::size_t i );
void la_vector_set( la_vector* v, std::size_t i, double x );
double* la_vector_ptr( la_vector* v, std::size_t i );
void la_vector_memcpy( la_vector* dst, const la_vector* src );
void la_vector_scale( la_vector* v, double c );

//! --- matrix (row-major) ---
la_matrix* la_matrix_alloc( std::size_t rows, std::size_t cols );
la_matrix* la_matrix_calloc( std::size_t rows, std::size_t cols );
void la_matrix_free( la_matrix* m );
double la_matrix_get( const la_matrix* m, std::size_t i, std::size_t j );
void la_matrix_set( la_matrix* m, std::size_t i, std::size_t j, double x );
void la_matrix_memcpy( la_matrix* dst, const la_matrix* src );
