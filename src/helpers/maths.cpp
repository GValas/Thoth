#include "maths.hpp"
#include "distributions.hpp"
#include "statistics.hpp"

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
    double G1 = GammaCdf( 1 / Strike, Alpha - 1, Beta );
    double G2 = GammaCdf( 1 / Strike, Alpha, Beta );
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
    double Nd1 = NormalCdf( d1 );
    double Nd2 = NormalCdf( d2 );
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
        double Nd1 = NormalCdf( d1 );
        double Nd2 = NormalCdf( d2 );
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

    //! positive-definite iff the Cholesky factorisation succeeds (on a copy)
    size_t n = m->size1;
    gsl_matrix* A = gsl_matrix_alloc( n, n );
    gsl_matrix_memcpy( A, m );
    bool positive = CholeskyDecomposeLower( A );
    gsl_matrix_free( A );

    return positive;
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

    // Natural (non-periodic) cubic spline solved in place (Burden & Faires):
    // second derivatives are zero at both ends, so a PDE price profile (which is
    // not periodic) gets the correct boundary condition. Matches gsl_interp_cspline.
    const size_t n = x_serie->size;
    vector<double> x( n ), y( n ), h( n - 1 ), m( n, 0.0 ), l( n ), mu( n ), z( n );
    for ( size_t i = 0; i < n; i++ )
    {
        x[i] = gsl_vector_get( x_serie, i );
        y[i] = gsl_vector_get( y_serie, i );
    }
    for ( size_t i = 0; i < n - 1; i++ )
    {
        h[i] = x[i + 1] - x[i];
    }
    // tridiagonal solve for the interior second derivatives m[1..n-2] (Thomas)
    l[0] = 1.0;
    mu[0] = 0.0;
    z[0] = 0.0;
    for ( size_t i = 1; i < n - 1; i++ )
    {
        double alpha = 3.0 * ( ( y[i + 1] - y[i] ) / h[i] - ( y[i] - y[i - 1] ) / h[i - 1] );
        l[i] = 2.0 * ( x[i + 1] - x[i - 1] ) - h[i - 1] * mu[i - 1];
        mu[i] = h[i] / l[i];
        z[i] = ( alpha - h[i - 1] * z[i - 1] ) / l[i];
    }
    l[n - 1] = 1.0;
    z[n - 1] = 0.0;
    for ( size_t j = n - 1; j-- > 0; )
    {
        m[j] = z[j] - mu[j] * m[j + 1];
    }
    // locate the interval [x[k], x[k+1]] containing x_point (clamp at the ends)
    size_t k = 0;
    while ( k < n - 2 && x_point > x[k + 1] )
    {
        k++;
    }
    const double dx = x_point - x[k];
    const double b = ( y[k + 1] - y[k] ) / h[k] - h[k] * ( 2.0 * m[k] + m[k + 1] ) / 6.0;
    const double c = m[k] / 2.0;
    const double d = ( m[k + 1] - m[k] ) / ( 6.0 * h[k] );
    return y[k] + dx * ( b + dx * ( c + dx * d ) );
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

//! in-place lower-triangular Cholesky factorisation: A = L * L^T, with L written
//! into the lower triangle (diagonal included), like gsl_linalg_cholesky_decomp.
//! Returns false if A is not positive-definite. The upper triangle is left
//! untouched (callers read only the lower triangle).
bool CholeskyDecomposeLower( gsl_matrix* A )
{
    const size_t n = A->size1;
    for ( size_t i = 0; i < n; i++ )
    {
        for ( size_t j = 0; j <= i; j++ )
        {
            double sum = gsl_matrix_get( A, i, j );
            for ( size_t k = 0; k < j; k++ )
            {
                sum -= gsl_matrix_get( A, i, k ) * gsl_matrix_get( A, j, k );
            }
            if ( i == j )
            {
                if ( sum <= 0.0 )
                {
                    return false; //!< not positive-definite
                }
                gsl_matrix_set( A, i, j, sqrt( sum ) );
            }
            else
            {
                gsl_matrix_set( A, i, j, sum / gsl_matrix_get( A, j, j ) );
            }
        }
    }
    return true;
}

//! solve a tridiagonal system M x = B by the Thomas algorithm. Diag has n
//! entries; Super (M[i][i+1]) and Sub (M[i+1][i]) have n-1. Replaces
//! gsl_linalg_solve_tridiag for the Crank-Nicolson PDE step (diagonally stable).
void SolveTridiagonal( const gsl_vector* Diag, const gsl_vector* Super,
                       const gsl_vector* Sub, const gsl_vector* B, gsl_vector* X )
{
    const size_t n = Diag->size;
    vector<double> c( n ), d( n ); //!< forward-swept super-diag and rhs
    double beta = gsl_vector_get( Diag, 0 );
    c[0] = ( n > 1 ? gsl_vector_get( Super, 0 ) : 0.0 ) / beta;
    d[0] = gsl_vector_get( B, 0 ) / beta;
    for ( size_t i = 1; i < n; i++ )
    {
        const double sub = gsl_vector_get( Sub, i - 1 );
        beta = gsl_vector_get( Diag, i ) - sub * c[i - 1];
        if ( i < n - 1 )
        {
            c[i] = gsl_vector_get( Super, i ) / beta;
        }
        d[i] = ( gsl_vector_get( B, i ) - sub * d[i - 1] ) / beta;
    }
    gsl_vector_set( X, n - 1, d[n - 1] );
    for ( size_t i = n - 1; i-- > 0; )
    {
        gsl_vector_set( X, i, d[i] - c[i] * gsl_vector_get( X, i + 1 ) );
    }
}

//! sum elments of vector
double ext_gsl_vector_sum( gsl_vector* v )
{
    return Sum( gsl_vector_ptr( v, 0 ), v->size );
}