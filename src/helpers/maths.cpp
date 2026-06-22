#include "maths.hpp"
#include "distributions.hpp"
#include "statistics.hpp"

//! constants
const size_t NEAR_POSITIVE_MATRIX_MAX_ITER = 24;  //!< bisection steps for the PD repair
const double NEAR_POSITIVE_MATRIX_MIN_EPS = 1e-6; //!< below this, treat the shift as none

// moment matching
//! Raw moments of the basket sum S = sum_i S_i with each S_i lognormal:
//!   M1 = sum F_i
//!   M2 = sum_{i,j} F_i F_j exp(vol_i vol_j rho_ij)
//!   M3 = triple sum with the analogous cross-covariance exponent
//! (the exp factor is E[S_i S_j]/(F_i F_j) for correlated lognormals).
void LN_to_M4( la_vector* Fwds,
               la_vector* Vols,
               la_matrix* Corr,
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
        M1 += la_vector_get( Fwds, i );
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
            M2 += la_vector_get( Fwds, i ) * la_vector_get( Fwds, j ) * exp( la_vector_get( Vols, i ) * la_vector_get( Vols, j ) * la_matrix_get( Corr, i, j ) );
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
                M3 += la_vector_get( Fwds, i ) * la_vector_get( Fwds, j ) * la_vector_get( Fwds, k ) * exp( la_vector_get( Vols, i ) * la_vector_get( Vols, j ) * la_matrix_get( Corr, i, j ) + la_vector_get( Vols, i ) * la_vector_get( Vols, k ) * la_matrix_get( Corr, i, k ) + la_vector_get( Vols, j ) * la_vector_get( Vols, k ) * la_matrix_get( Corr, j, k ) );
            }
        }
    }
}

//! inverse gamma call price: if 1/S ~ Gamma(Alpha, Beta) then S is inverse-gamma;
//! the call is df*(F*G1 - K*G2) with G1, G2 gamma CDFs at 1/K under shifted shapes
//! (the Alpha-1 shift comes from the extra S factor in the first integral).
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

//! method-of-moments inversion (M1, M2) -> inverse-gamma (Alpha shape, Beta scale)
void M2_to_IG( const double M1,
               const double M2,
               double& Alpha,
               double& Beta )
{
    //! M2 - M1^2 is the variance; the closed forms below are the standard IG
    //! moment-matching equations (shape from the variance-to-mean ratio)
    Alpha = ( 2 * M2 - M1 * M1 ) / ( M2 - M1 * M1 );
    Beta = ( M2 - M1 * M1 ) / ( M2 * M1 );
}

//! lognormal call price = Black-76 with TOTAL variance Var (= sigma^2 T); here Var
//! already aggregates the maturity, so d1/d2 use Var directly (no extra *T).
double LN_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double /*Mu*/,
                      const double Var )
{
    //! d1 = [ln(F/K) + Var/2] / sqrt(Var), d2 = [ln(F/K) - Var/2] / sqrt(Var)
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
    //! put/call parity P = C - df*(F - K)
    double c = LN_Call_Price( Forward, Strike, DiscountFactor, Mu, Var );
    return c - DiscountFactor * ( Forward - Strike );
}

//! (M1, M2) -> lognormal total variance Var = ln(M2 / M1^2). Mu is implied by the
//! forward (M1) and not returned here.
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

    //! convert raw moments to central moments: E1 mean, E2 variance, E3 third
    //! central moment (skew numerator)
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

    //! closed-form shifted-lognormal moment match (3 central moments -> shift D,
    //! lognormal Mu/Var). x = sign-preserving cube root of y; z is the location of
    //! the lognormal part, and D = mean - z is the shift. The max(0,.) guards the
    //! square roots against tiny negative round-off.
    double x, y, z;
    y = ( 0.5 * ( ( E2 * E2 * E2 ) / ( E3 * E3 ) ) * ( E3 + 2 * E2 * E2 * E2 / E3 + sqrt( max( 0.0, 4 * E2 * E2 * E2 + E3 * E3 ) ) ) );
    x = ( y == 0 ? 0 : 1 ) * ( y < 0 ? -1 : 1 ) * exp( log( fabs( y ) ) / 3 ); //!< signed cbrt(y)
    z = x + E2 * E2 / E3 * ( 1 + E2 * E2 / ( E3 * x ) );

    D = E1 - z;                                         //!< the SLN shift
    Mu = log( z * z / sqrt( max( 0.0, E2 + z * z ) ) ); //!< lognormal log-mean
    Var = log( 1 + E2 / ( z * z ) );                    //!< lognormal log-variance
}

//! shifted lognormal call price
double SLN_Call_Price( const double /*Forward*/,
                       const double Strike,
                       const double DiscountFactor,
                       const double Mu,
                       const double Var,
                       const double D )
{
    //! shift already above the strike: the lognormal part (always > 0) keeps the
    //! option in-the-money with certainty, so the price is the discounted forward
    //! value D + E[lognormal] - K (E[lognormal] = exp(Mu + Var/2))
    if ( D >= Strike )
    {
        return DiscountFactor * ( D + exp( Mu + 0.5 * Var ) - Strike );
    }

    else
    {
        //! otherwise a Black-76 on the SHIFTED strike (Strike - D): the lognormal
        //! variable must exceed (Strike - D) for the option to pay
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
    //! put/call parity P = C - df*(F - K)
    double c = SLN_Call_Price( Forward, Strike, DiscountFactor, Mu, Var, D );
    return c - DiscountFactor * ( Forward - Strike );
}

//! test if matrix is square
bool ext_la_matrix_is_square( const la_matrix* m )
{
    return ( m->size1 == m->size2 );
}

//! shift matrix to eps : ( 1 - e ).M + e.I — shrink off-diagonals toward 0 (the
//! diagonal stays 1 for a correlation matrix), nudging it toward positive-definite
void ext_la_matrix_shift_to_epsilon( la_matrix* m, double eps )
{
    for ( size_t i = 0; i < m->size1; i++ )
    {
        for ( size_t j = 0; j < m->size2; j++ )
        {
            if ( i != j )
            {
                la_matrix_set( m, i, j, la_matrix_get( m, i, j ) * ( 1 - eps ) );
            }
        }
    }
}

//! robustification: bisection-search a blend eps in [0,1] that makes the
//! eps-shifted matrix positive-definite, then apply the upper-bracket shift in
//! place (the out-param eps carries the last bisection midpoint).
void ext_la_matrix_to_near_positive( la_matrix* m, double& eps )
{

    //! RAII scratch copy (freed on every exit path)
    size_t n = m->size1;
    LaMatrix A = la_matrix_alloc( n, n );
    la_matrix_memcpy( A, m );

    //! bisection bracket: eps=0 may not be PD, eps=1 (the identity) always is
    double eps_min = 0;
    double eps_max = 1;

    //! shrink the bracket toward the smallest PD-feasible eps
    size_t i = 0;
    do
    {
        eps = ( eps_max + eps_min ) / 2;
        la_matrix_memcpy( A, m );
        ext_la_matrix_shift_to_epsilon( A, eps );

        if ( ext_la_matrix_is_positive( A ) )
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

    //! a shift below the tolerance is noise: report 0 and leave m untouched;
    //! otherwise commit the shift (eps_max is the PD-feasible upper bracket)
    if ( eps_max < NEAR_POSITIVE_MATRIX_MIN_EPS )
    {
        eps_max = 0;
    }
    else
    {
        ext_la_matrix_shift_to_epsilon( m, eps_max );
    }
}

//! test if matrix is symmetric
bool ext_la_matrix_is_symmetric( const la_matrix* m )
{

    //! square matrix only
    if ( !ext_la_matrix_is_square( m ) )
    {
        return false;
    }

    //! symmetry
    size_t n = m->size1;
    for ( size_t i = 1; i < n; i++ )
    {
        for ( size_t j = 0; j < i; j++ )
        {
            if ( la_matrix_get( m, i, j ) != la_matrix_get( m, j, i ) )
            {
                return false;
            }
        }
    }

    //! all tests are ok
    return true;
}

//! test if matrix is positive
bool ext_la_matrix_is_positive( const la_matrix* m )
{
    //! square matrix
    if ( !ext_la_matrix_is_symmetric( m ) )
    {
        return false;
    }

    //! positive-definite iff the Cholesky factorisation succeeds (on a copy)
    size_t n = m->size1;
    LaMatrix A = la_matrix_alloc( n, n ); //!< RAII: freed on return
    la_matrix_memcpy( A, m );
    return CholeskyDecomposeLower( A );
}

//!
double InterpolateWithSpline( la_vector* x_serie,
                              la_vector* y_serie,
                              double x_point )
{

    // Natural (non-periodic) cubic spline solved in place (Burden & Faires):
    // second derivatives are zero at both ends, so a PDE price profile (which is
    // not periodic) gets the correct boundary condition. Matches gsl_interp_cspline.
    const size_t n = x_serie->size;
    vector<double> x( n ), y( n ), h( n - 1 ), m( n, 0.0 ), l( n ), mu( n ), z( n );
    for ( size_t i = 0; i < n; i++ )
    {
        x[i] = la_vector_get( x_serie, i );
        y[i] = la_vector_get( y_serie, i );
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
        //! alpha = the spline RHS (difference of secant slopes); l/mu/z are the
        //! forward-elimination factors of the tridiagonal moment system
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
    //! evaluate the cubic piece y[k] + b*dx + c*dx^2 + d*dx^3 (Horner) where
    //! b/c/d are built from the end-of-interval second derivatives m[k], m[k+1]
    const double dx = x_point - x[k];
    const double b = ( y[k + 1] - y[k] ) / h[k] - h[k] * ( 2.0 * m[k] + m[k + 1] ) / 6.0;
    const double c = m[k] / 2.0;
    const double d = ( m[k + 1] - m[k] ) / ( 6.0 * h[k] );
    return y[k] + dx * ( b + dx * ( c + dx * d ) );
}

//!
LaMatrix ToLaMatrix( const vector<double>& Matrix )
{
    size_t n = (size_t)sqrt( (double)Matrix.size() );
    LaMatrix m = la_matrix_alloc( n, n );
    for ( size_t i = 0; i < n; i++ )
    {
        for ( size_t j = 0; j < n; j++ )
        {
            la_matrix_set( m, i, j, Matrix[i * n + j] );
        }
    }
    return m;
}

//!
vector<double> FromLaMatrix( la_matrix* Matrix )
{
    vector<double> m;
    for ( size_t i = 0; i < Matrix->size1; i++ )
    {
        for ( size_t j = 0; j < Matrix->size2; j++ )
        {
            m.push_back( la_matrix_get( Matrix, i, j ) );
        }
    }
    return m;
}

//!
la_matrix* ext_la_vector_to_matrix( const la_vector* v,
                                    size_t row_size )
{
    //! Reshape the flat vector into an owning matrix.  We copy rather than
    //! alias v's storage: the returned matrix is later released with
    //! la_matrix_free(), which must own (and be allowed to free) its block.
    size_t col_size = v->size / row_size;
    la_matrix* m = la_matrix_alloc( col_size, row_size );
    for ( size_t i = 0; i < col_size; i++ )
    {
        for ( size_t j = 0; j < row_size; j++ )
        {
            la_matrix_set( m, i, j, la_vector_get( v, i * row_size + j ) );
        }
    }
    return m;
}

//! in-place lower-triangular Cholesky factorisation: A = L * L^T, with L written
//! into the lower triangle (diagonal included), like gsl_linalg_cholesky_decomp.
//! Returns false if A is not positive-definite. The upper triangle is left
//! untouched (callers read only the lower triangle).
bool CholeskyDecomposeLower( la_matrix* A )
{
    const size_t n = A->size1;
    for ( size_t i = 0; i < n; i++ )
    {
        for ( size_t j = 0; j <= i; j++ )
        {
            double sum = la_matrix_get( A, i, j );
            for ( size_t k = 0; k < j; k++ )
            {
                sum -= la_matrix_get( A, i, k ) * la_matrix_get( A, j, k );
            }
            if ( i == j )
            {
                if ( sum <= 0.0 )
                {
                    return false; //!< not positive-definite
                }
                la_matrix_set( A, i, j, sqrt( sum ) );
            }
            else
            {
                la_matrix_set( A, i, j, sum / la_matrix_get( A, j, j ) );
            }
        }
    }
    return true;
}

//! solve a tridiagonal system M x = B by the Thomas algorithm. Diag has n
//! entries; Super (M[i][i+1]) and Sub (M[i+1][i]) have n-1. Replaces
//! gsl_linalg_solve_tridiag for the Crank-Nicolson PDE step (diagonally stable).
void SolveTridiagonal( const la_vector* Diag, const la_vector* Super,
                       const la_vector* Sub, const la_vector* B, la_vector* X )
{
    const size_t n = Diag->size;
    vector<double> c( n ), d( n ); //!< forward-swept super-diag and rhs
    double beta = la_vector_get( Diag, 0 );
    c[0] = ( n > 1 ? la_vector_get( Super, 0 ) : 0.0 ) / beta;
    d[0] = la_vector_get( B, 0 ) / beta;
    for ( size_t i = 1; i < n; i++ )
    {
        const double sub = la_vector_get( Sub, i - 1 );
        beta = la_vector_get( Diag, i ) - sub * c[i - 1];
        if ( i < n - 1 )
        {
            c[i] = la_vector_get( Super, i ) / beta;
        }
        d[i] = ( la_vector_get( B, i ) - sub * d[i - 1] ) / beta;
    }
    la_vector_set( X, n - 1, d[n - 1] );
    for ( size_t i = n - 1; i-- > 0; )
    {
        la_vector_set( X, i, d[i] - c[i] * la_vector_get( X, i + 1 ) );
    }
}

//! ordinary least squares: minimise ||X beta - y||^2 via the normal equations
//! (X^T X) beta = X^T y, solved by Cholesky. X is n x p with p small and full
//! column rank (the Longstaff-Schwartz {1, m, m^2} basis). Returns beta (size p).
//! Replaces gsl_multifit_linear (only the coefficients are needed, not cov/chisq).
//! Ordinary least squares via the normal equations (X^T X) beta = X^T y, solved by
//! Cholesky. Returns an EMPTY vector when X^T X is numerically singular (e.g. a
//! degenerate regression design) so the caller can fall back instead of dividing
//! by a zero pivot and propagating NaNs.
vector<double> LeastSquares( const la_matrix* X, const la_vector* y )
{
    const size_t n = X->size1;
    const size_t p = X->size2;
    LaMatrix A = la_matrix_alloc( p, p ); //!< normal matrix X^T X (SPD), RAII-freed
    vector<double> g( p, 0.0 );           //!< right-hand side X^T y
    for ( size_t a = 0; a < p; a++ )
    {
        for ( size_t b = 0; b <= a; b++ )
        {
            double s = 0.0;
            for ( size_t k = 0; k < n; k++ )
            {
                s += la_matrix_get( X, k, a ) * la_matrix_get( X, k, b );
            }
            la_matrix_set( A, a, b, s );
            la_matrix_set( A, b, a, s );
        }
        double sg = 0.0;
        for ( size_t k = 0; k < n; k++ )
        {
            sg += la_matrix_get( X, k, a ) * la_vector_get( y, k );
        }
        g[a] = sg;
    }
    //! A = L L^T, then solve L z = g (forward) and L^T beta = z (back). A singular
    //! normal matrix has no factorisation -> signal failure with an empty result.
    if ( !CholeskyDecomposeLower( A ) )
    {
        return {};
    }
    vector<double> z( p ), beta( p );
    for ( size_t i = 0; i < p; i++ )
    {
        double s = g[i];
        for ( size_t k = 0; k < i; k++ )
        {
            s -= la_matrix_get( A, i, k ) * z[k];
        }
        z[i] = s / la_matrix_get( A, i, i );
    }
    for ( size_t i = p; i-- > 0; )
    {
        double s = z[i];
        for ( size_t k = i + 1; k < p; k++ )
        {
            s -= la_matrix_get( A, k, i ) * beta[k]; //!< L^T[i][k] = L[k][i]
        }
        beta[i] = s / la_matrix_get( A, i, i );
    }
    return beta; //!< A is RAII-freed at scope exit
}

//! sum elments of vector
double ext_la_vector_sum( la_vector* v )
{
    return Sum( la_vector_ptr( v, 0 ), v->size );
}