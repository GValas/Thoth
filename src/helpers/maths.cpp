#include "maths.hpp"

//! constants
const size_t NEAR_POSITIVE_MATRIX_MAX_ITER = 24;
const double NEAR_POSITIVE_MATRIX_MIN_EPS = 1e-6;

// moment matching
void LN_to_M4( gsl_vector* Fwds,
               gsl_vector* Vols,
               gsl_matrix* Corr,
               double& M1,
               double& M2,
               double& M3,
               double& /*M4*/ )
{

    size_t n = Fwds->size;

    // 1st order
    M1 = 0;
    for ( size_t i = 0;
          i < n;
          i++ )
    {
        M1 += gsl_vector_get( Fwds, i );
    }

    // 2nd order
    M2 = 0;
    for ( size_t i = 0;
          i < n;
          i++ )
    {
        for ( size_t j = 0;
              j < n;
              j++ )
        {
            M2 += gsl_vector_get( Fwds, i ) * gsl_vector_get( Fwds, j ) * exp( gsl_vector_get( Vols, i ) * gsl_vector_get( Vols, j ) * gsl_matrix_get( Corr, i, j ) );
        }
    }

    // 3rd order
    M3 = 0;
    for ( size_t i = 0;
          i < n;
          i++ )
    {
        for ( size_t j = 0;
              j < n;
              j++ )
        {
            for ( size_t k = 0;
                  k < n;
                  k++ )
            {
                M3 += gsl_vector_get( Fwds, i ) * gsl_vector_get( Fwds, j ) * gsl_vector_get( Fwds, k ) * exp( gsl_vector_get( Vols, i ) * gsl_vector_get( Vols, j ) * gsl_matrix_get( Corr, i, j ) + gsl_vector_get( Vols, i ) * gsl_vector_get( Vols, k ) * gsl_matrix_get( Corr, i, k ) + gsl_vector_get( Vols, j ) * gsl_vector_get( Vols, k ) * gsl_matrix_get( Corr, j, k ) );
            }
        }
    }
}

//! inverse gamma call price
double IG_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Alpha,
                      const double Beta )
{
    double G1 = gsl_cdf_gamma_P( 1 / Strike, Alpha - 1, Beta );
    double G2 = gsl_cdf_gamma_P( 1 / Strike, Alpha, Beta );
    return DiscountFactor * ( Forward * G1 - Strike * G2 );
}

//!
void M2_to_IG( const double M1,
               const double M2,
               double& Alpha,
               double& Beta )
{
    Alpha = ( 2 * M2 - M1 * M1 ) / ( M2 - M1 * M1 );
    Beta = ( M2 - M1 * M1 ) / ( M2 * M1 );
}

//! lognormal call price
double LN_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double /*Mu*/,
                      const double Var )
{
    double d1 = ( log( Forward / Strike ) + Var / 2 ) / sqrt( Var );
    double d2 = ( log( Forward / Strike ) - Var / 2 ) / sqrt( Var );
    double Nd1 = gsl_cdf_gaussian_P( d1, 1 );
    double Nd2 = gsl_cdf_gaussian_P( d2, 1 );
    return DiscountFactor * ( Forward * Nd1 - Strike * Nd2 );
}

double LN_Put_Price( const double Forward,
                     const double Strike,
                     const double DiscountFactor,
                     const double Mu,
                     const double Var )
{
    double c = LN_Call_Price( Forward, Strike, DiscountFactor, Mu, Var );
    return c - DiscountFactor * ( Forward - Strike );
}

//!
void M2_to_LN( const double M1,
               const double M2,
               double& /*Mu*/,
               double& Var )
{
    Var = log( M2 / ( M1 * M1 ) );
}

void M3_to_SLN( const double M1,
                const double M2,
                const double M3,
                double& Mu,
                double& Var,
                double& D )
{

    // (M1, M2) -> (Mu, Var, D)
    double E1, E2, E3;
    E1 = M1;
    E2 = M2 - E1 * E1;
    E3 = M3 - 3 * E1 * M2 + 2 * E1 * E1 * E1;

    if ( E2 <= 0 )
    {
        ERR( "M3_to_SLN: non-positive variance (E2 <= 0); check the basket "
             "forward/vol/correlation inputs" );
    }

    //! near-symmetric (third central moment ~ 0): the shifted-lognormal shift
    //! degenerates, so fall back to the plain lognormal match and avoid the
    //! 1/E3 division below. Tolerance is relative to the variance scale.
    if ( fabs( E3 ) < 1e-10 * E2 * sqrt( E2 ) )
    {
        M2_to_LN( M1, M2, Mu, Var );
        D = 0;
        return;
    }

    double x, y, z;
    y = ( 0.5 * ( ( E2 * E2 * E2 ) / ( E3 * E3 ) ) * ( E3 + 2 * E2 * E2 * E2 / E3 + sqrt( max( 0.0, 4 * E2 * E2 * E2 + E3 * E3 ) ) ) );
    x = ( y == 0 ? 0 : 1 ) * ( y < 0 ? -1 : 1 ) * exp( log( abs( y ) ) / 3 );
    z = x + E2 * E2 / E3 * ( 1 + E2 * E2 / ( E3 * x ) );

    D = E1 - z;
    Mu = log( z * z / sqrt( max( 0.0, E2 + z * z ) ) );
    Var = log( 1 + E2 / ( z * z ) );
}

//! shifted lognormal call price
double SLN_Call_Price( const double /*Forward*/,
                       const double Strike,
                       const double DiscountFactor,
                       const double Mu,
                       const double Var,
                       const double D )
{
    if ( D >= Strike )
    {
        return DiscountFactor * ( D + exp( Mu + 0.5 * Var ) - Strike );
    }

    else
    {
        double v = sqrt( Var );
        double d1 = ( -log( Strike - D ) + Mu + Var ) / v;
        double d2 = d1 - v;
        double Nd1 = gsl_cdf_gaussian_P( d1, 1 );
        double Nd2 = gsl_cdf_gaussian_P( d2, 1 );
        return DiscountFactor * ( exp( Mu + 0.5 * Var ) * Nd1 - ( Strike - D ) * Nd2 );
    }
}

//! shifted lognormal put price
double SLN_Put_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Mu,
                      const double Var,
                      const double D )
{
    double c = SLN_Call_Price( Forward, Strike, DiscountFactor, Mu, Var, D );
    return c - DiscountFactor * ( Forward - Strike );
}

//! test if matrix is square
bool ext_gsl_matrix_is_square( const gsl_matrix* m )
{
    return ( m->size1 == m->size2 );
}

//! shift matrix to eps : ( 1 - e ).M + e.I
void ext_gsl_matrix_shift_to_epsilon( gsl_matrix* m, double eps )
{
    for ( size_t i = 0; i < m->size1; i++ )
    {
        for ( size_t j = 0; j < m->size2; j++ )
        {
            if ( i != j )
            {
                gsl_matrix_set( m, i, j, gsl_matrix_get( m, i, j ) * ( 1 - eps ) );
            }
        }
    }
}

//! robustification
void ext_gsl_matrix_to_near_positive( gsl_matrix* m, double& eps )
{

    //! RAII scratch copy (freed on every exit path)
    size_t n = m->size1;
    GslMatrix A = gsl_matrix_alloc( n, n );
    gsl_matrix_memcpy( A, m );

    //!
    double eps_min = 0;
    double eps_max = 1;

    //!
    size_t i = 0;
    do
    {
        eps = ( eps_max + eps_min ) / 2;
        gsl_matrix_memcpy( A, m );
        ext_gsl_matrix_shift_to_epsilon( A, eps );

        if ( ext_gsl_matrix_is_positive( A ) )
        {
            eps_max = eps;
        }
        else
        {
            eps_min = eps;
        }
        i++;
    } while ( i < NEAR_POSITIVE_MATRIX_MAX_ITER &&
              ( eps_max - eps_min ) > NEAR_POSITIVE_MATRIX_MIN_EPS );

    //! small epsilon => null
    if ( eps_max < NEAR_POSITIVE_MATRIX_MIN_EPS )
    {
        eps_max = 0;
    }
    else
    {
        ext_gsl_matrix_shift_to_epsilon( m, eps_max );
    }
}

//! test if matrix is symmetric
bool ext_gsl_matrix_is_symmetric( const gsl_matrix* m )
{

    //! square matrix only
    if ( !ext_gsl_matrix_is_square( m ) )
    {
        return false;
    }

    //! symmetry
    size_t n = m->size1;
    for ( size_t i = 1; i < n; i++ )
    {
        for ( size_t j = 0; j < i; j++ )
        {
            if ( gsl_matrix_get( m, i, j ) != gsl_matrix_get( m, j, i ) )
            {
                return false;
            }
        }
    }

    //! all tests are ok
    return true;
}

//! test if matrix is positive
bool ext_gsl_matrix_is_positive( const gsl_matrix* m )
{
    //! square matrix
    if ( !ext_gsl_matrix_is_symmetric( m ) )
    {
        return false;
    }

    //! try to use cholesky
    size_t n = m->size1;
    gsl_matrix* A = gsl_matrix_alloc( n, n );
    gsl_matrix_memcpy( A, m );
    int gsl_error = gsl_linalg_cholesky_decomp( A );
    gsl_matrix_free( A );

    return gsl_error != GSL_EDOM;
}

//!
vector<double> ToSymmetricMatrix( const vector<double>& Matrix )
{
    vector<double> v;
    size_t n = (size_t)sqrt( (double)Matrix.size() );
    for ( size_t i = 0; i < n - 1; i++ )
    {
        for ( size_t j = i + 1; j < n; j++ )
        {
            v.push_back( Matrix[i * n + j] );
        }
    }
    return v;
}

//! symmetric to full matrix
vector<double> FromSymmetricMatrix( const vector<double>& Matrix )
{
    vector<double> v;
    size_t n = Matrix.size(); // n -> n(n-1)/2
    n = (size_t)( 1 + sqrt( 1 + 8. * n ) ) / 2;
    for ( size_t i = 0; i < n; i++ )
    {
        for ( size_t j = 0; j < n; j++ )
        {
            if ( i == j )
            {
                v.push_back( 1 );
            }
            else
            {
                size_t k = ( i > j ) ? i * ( i - 1 ) / 2 + j : j * ( j - 1 ) / 2 + i;
                v.push_back( Matrix[k] );
            }
        }
    }
    return v;
}

//!
double InterpolateWithSpline( gsl_vector* x_serie,
                              gsl_vector* y_serie,
                              double x_point )
{

    // allocations. Natural (non-periodic) cubic spline: the data (e.g. a PDE
    // price profile) is not periodic, so cspline_periodic — which assumes
    // y[0] == y[n-1] — would be the wrong boundary condition.
    gsl_interp_accel* acc = gsl_interp_accel_alloc();
    const gsl_interp_type* t = gsl_interp_cspline;
    gsl_spline* spline = gsl_spline_alloc( t, x_serie->size );
    gsl_spline_init( spline, gsl_vector_ptr( x_serie, 0 ), gsl_vector_ptr( y_serie, 0 ), x_serie->size );

    // interpolate
    double y_point = gsl_spline_eval( spline, x_point, acc );

    // free only what this function allocated; inputs stay owned by the caller
    gsl_spline_free( spline );
    gsl_interp_accel_free( acc );

    return y_point;
}

//!
gsl_matrix* ToGslMatrix( const vector<double>& Matrix )
{
    size_t n = (size_t)sqrt( (double)Matrix.size() );
    gsl_matrix* m = gsl_matrix_alloc( n, n );
    for ( size_t i = 0; i < n; i++ )
    {
        for ( size_t j = 0; j < n; j++ )
        {
            gsl_matrix_set( m, i, j, Matrix[i * n + j] );
        }
    }
    return m;
}

//!
vector<double> FromGslMatrix( gsl_matrix* Matrix )
{
    vector<double> m;
    for ( size_t i = 0; i < Matrix->size1; i++ )
    {
        for ( size_t j = 0; j < Matrix->size2; j++ )
        {
            m.push_back( gsl_matrix_get( Matrix, i, j ) );
        }
    }
    return m;
}

//!
gsl_matrix* ext_gsl_vector_to_matrix( const gsl_vector* v,
                                      size_t row_size )
{
    //! Reshape the flat vector into an owning matrix.  We copy rather than
    //! alias v's storage: the returned matrix is later released with
    //! gsl_matrix_free(), which must own (and be allowed to free) its block.
    size_t col_size = v->size / row_size;
    gsl_matrix* m = gsl_matrix_alloc( col_size, row_size );
    for ( size_t i = 0; i < col_size; i++ )
    {
        for ( size_t j = 0; j < row_size; j++ )
        {
            gsl_matrix_set( m, i, j, gsl_vector_get( v, i * row_size + j ) );
        }
    }
    return m;
}

//! computes the weighted correlation, given means and variances
double ext_gsl_stats_wcorrelation_m_v( const double w[],
                                       size_t wstride,
                                       const double data1[],
                                       size_t stride1,
                                       const double data2[],
                                       size_t stride2,
                                       size_t n,
                                       double wmean1,
                                       double wmean2,
                                       double wvariance1,
                                       double wvariance2 )
{
    double cov = ext_gsl_stats_wcovariance_m_v( w,
                                                wstride,
                                                data1,
                                                stride1,
                                                data2,
                                                stride2,
                                                n,
                                                wmean1,
                                                wmean2 );

    return cov / sqrt( wvariance1 * wvariance2 );
}

//! computes the weighted covariance, given means
double ext_gsl_stats_wcovariance_m_v( const double weights[],
                                      size_t wstride,
                                      const double data1[],
                                      size_t stride1,
                                      const double data2[],
                                      size_t stride2,
                                      size_t n,
                                      double wmean1,
                                      double wmean2 )
{
    double sum = 0;
    double sum_w2 = 0;
    double sum_w = 0;
    for ( size_t k = 0; k < n; k++ )
    {
        double w = weights[k * wstride];
        sum += w * ( data1[k * stride1] - wmean1 ) * ( data2[k * stride2] - wmean2 );
        sum_w += w;
        sum_w2 += w * w;
    }
    double bias = sum_w / ( sum_w * sum_w - sum_w2 );
    return sum * bias;
}

//! log gsl_vector
void ext_gsl_vector_log( gsl_vector* v )
{
    ofstream log_file( GSL_LOG, ios::out );
    log_file << setprecision( DECIMAL_PRECISION );
    for ( size_t i = 0; i < v->size; i++ )
    {
        log_file << gsl_vector_get( v, i ) << endl;
    }
    log_file.close();
}

//! log gsl_matrix
void ext_gsl_matrix_log( gsl_matrix* m )
{
    ofstream log_file( GSL_LOG, ios::out );
    log_file << setprecision( DECIMAL_PRECISION );
    for ( size_t i = 0; i < m->size1; i++ )
    {
        for ( size_t j = 0; j < m->size2; j++ )
        {
            log_file << gsl_matrix_get( m, i, j ) << "\t";
        }
        log_file << endl;
    }
    log_file.close();
}

//! sum elments of vector
double ext_gsl_vector_sum( gsl_vector* v )
{
    size_t n = v->size;
    return gsl_stats_mean( gsl_vector_ptr( v, 0 ), 1, n ) * n;
}