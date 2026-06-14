#include "maths.hpp"
#include <cmath>
#include <doctest/doctest.h>

using doctest::Approx;

TEST_CASE( "matrix predicates: square, symmetric, positive" )
{
    gsl_matrix* id = ToGslMatrix( { 1, 0, 0, 0, 1, 0, 0, 0, 1 } );
    CHECK( ext_gsl_matrix_is_square( id ) );
    CHECK( ext_gsl_matrix_is_symmetric( id ) );
    CHECK( ext_gsl_matrix_is_positive( id ) );
    gsl_matrix_free( id );

    // a genuine 2x2 correlation is positive semi-definite
    gsl_matrix* c = ToGslMatrix( { 1, 0.5, 0.5, 1 } );
    CHECK( ext_gsl_matrix_is_positive( c ) );
    gsl_matrix_free( c );

    // an impossible "correlation" (|rho| > 1) is not PSD
    gsl_matrix* bad = ToGslMatrix( { 1, 1.5, 1.5, 1 } );
    CHECK_FALSE( ext_gsl_matrix_is_positive( bad ) );
    gsl_matrix_free( bad );
}

TEST_CASE( "ext_gsl_matrix_to_near_positive repairs a non-PSD correlation" )
{
    gsl_matrix* m = ToGslMatrix( { 1, 1.5, 1.5, 1 } );
    REQUIRE_FALSE( ext_gsl_matrix_is_positive( m ) );

    double eps = 0;
    ext_gsl_matrix_to_near_positive( m, eps );

    CHECK( ext_gsl_matrix_is_positive( m ) );          //!< now repaired
    CHECK( eps > 0 );                                  //!< a strictly positive shift was needed
    CHECK( gsl_matrix_get( m, 0, 0 ) == Approx( 1 ) ); //!< unit diagonal preserved
    gsl_matrix_free( m );
}

TEST_CASE( "asinh inverts sinh" )
{
    for ( double x : { -2.0, -0.5, 0.0, 1.0, 3.0 } )
    {
        CAPTURE( x );
        CHECK( asinh( std::sinh( x ) ) == Approx( x ).epsilon( 1e-9 ) );
    }
}

TEST_CASE( "InterpolateWithSpline passes through its sample points" )
{
    // periodic cubic spline (endpoints equal); it still interpolates the nodes
    // exactly. NB: InterpolateWithSpline frees its x vector, so allocate fresh.
    auto interp = []( double q )
    {
        gsl_vector* x = gsl_vector_alloc( 5 );
        gsl_vector* y = gsl_vector_alloc( 5 );
        double xs[5] = { 0, 1, 2, 3, 4 };
        double ys[5] = { 0, 1, 0, 1, 0 };
        for ( int i = 0; i < 5; i++ )
        {
            gsl_vector_set( x, i, xs[i] );
            gsl_vector_set( y, i, ys[i] );
        }
        double r = InterpolateWithSpline( x, y, q );
        gsl_vector_free( y );
        return r;
    };

    CHECK( interp( 1.0 ) == Approx( 1.0 ) );
    CHECK( interp( 3.0 ) == Approx( 1.0 ) );
    CHECK( interp( 2.0 ) == Approx( 0.0 ) );
}
