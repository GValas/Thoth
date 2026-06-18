#pragma once
#include "thoth.hpp"

double IG_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Alpha,
                      const double Beta );

double LN_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Mu,
                      const double Var );

double LN_Put_Price( const double Forward,
                     const double Strike,
                     const double DiscountFactor,
                     const double Mu,
                     const double Var );

double SLN_Call_Price( const double Forward,
                       const double Strike,
                       const double DiscountFactor,
                       const double Mu,
                       const double Var,
                       const double D );

double SLN_Put_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Mu,
                      const double Var,
                      const double D );

void M2_to_IG( const double M1,
               const double M2,
               double& Alpha,
               double& Beta );

void M2_to_LN( const double M1,
               const double M2,
               double& Mu,
               double& Var );

void M3_to_SLN( const double M1,
                const double M2,
                const double M3,
                double& Mu,
                double& Var,
                double& D );

void LN_to_M4( la_vector* Fwds,
               la_vector* Vols,
               la_matrix* Corr,
               double& M1,
               double& M2,
               double& M3,
               double& M4 );

la_matrix* ext_la_vector_to_matrix( const la_vector* v,
                                    size_t row_size );
//! in-place lower-triangular Cholesky (A = L L^T); false if not positive-definite
[[nodiscard]] bool CholeskyDecomposeLower( la_matrix* A );
//! solve a tridiagonal system M X = B (Thomas algorithm)
void SolveTridiagonal( const la_vector* Diag, const la_vector* Super,
                       const la_vector* Sub, const la_vector* B, la_vector* X );
//! ordinary least squares (normal equations + Cholesky); returns beta (size X cols)
[[nodiscard]] vector<double> LeastSquares( const la_matrix* X, const la_vector* y );
[[nodiscard]] bool ext_la_matrix_is_positive( const la_matrix* m );
[[nodiscard]] bool ext_la_matrix_is_square( const la_matrix* m );
[[nodiscard]] bool ext_la_matrix_is_symmetric( const la_matrix* m );
void ext_la_matrix_to_near_positive( la_matrix* m,
                                     double& eps );
void ext_la_matrix_shift_to_epsilon( la_matrix* m,
                                     double eps );
void ext_la_matrix_from_symmetric( la_matrix* m );
void ext_la_matrix_to_symmetric( la_matrix* m );

double ext_la_vector_sum( la_vector* v );

double ext_stats_wcovariance_m( const double w[],
                                const double data1[],
                                const size_t stride1,
                                const double data2[],
                                const size_t stride2,
                                const size_t n,
                                const double wmean1,
                                const double wmean2 );

double ext_stats_wcorrelation_m_v( const double w[],
                                   const double data1[],
                                   const size_t stride1,
                                   const double data2[],
                                   const size_t stride2,
                                   const size_t n,
                                   const double wmean1,
                                   const double wmean2,
                                   const double wvar1,
                                   const double wvar2 );

//! computes the weighted covariance, given means
double ext_stats_wcovariance_m_v( const double w[],
                                  size_t wstride,
                                  const double data1[],
                                  size_t stride1,
                                  const double data2[],
                                  size_t stride2,
                                  size_t n,
                                  double wmean1,
                                  double wmean2 );

vector<double> ToSymmetricMatrix( const vector<double>& Matrix );
vector<double> FromSymmetricMatrix( const vector<double>& Matrix );
LaMatrix ToLaMatrix( const vector<double>& Matrix ); //!< caller-owned (RAII)
vector<double> FromLaMatrix( la_matrix* Matrix );

//
double InterpolateWithSpline( la_vector* x_serie, la_vector* y_serie, double x_point );

//! computes the weighted correlation, given means and variances
double ext_stats_wcorrelation_m_v( const double w[],
                                   size_t wstride,
                                   const double data1[],
                                   size_t stride1,
                                   const double data2[],
                                   size_t stride2,
                                   size_t n,
                                   double wmean1,
                                   double wmean2,
                                   double wvariance1,
                                   double wvariance2 );