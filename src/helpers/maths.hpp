#pragma once
#include "thoth.hpp"

//! ----------------------------------------------------------------------
//! Basket analytics by MOMENT MATCHING plus the linear-algebra helpers they need.
//!
//! A lognormal basket has no closed form, so the engine matches the first few
//! moments of the true basket distribution to a tractable proxy and prices an
//! option on that proxy:
//!   - 2 moments -> Lognormal (LN) or Inverse-Gamma (IG)
//!   - 3 moments -> Shifted-Lognormal (SLN), which captures skew via a shift D
//! LN_to_M4 computes the basket's raw moments from the per-asset forwards/vols and
//! the correlation matrix; the M*_to_* routines invert moments back to proxy
//! parameters; the *_Call/Put_Price routines then price under the proxy.
//!
//! The rest of the file is the dense-linear-algebra toolkit (Cholesky, tridiagonal
//! solve, OLS, near-PD correlation repair, cubic spline, weighted stats) carried
//! over from GSL, operating on the la_vector / la_matrix containers.
//! ----------------------------------------------------------------------

//! Inverse-gamma proxy call price (2-moment match). Alpha/Beta are the IG shape
//! and scale from M2_to_IG. Forward-measure: df discounts the payoff.
double IG_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Alpha,
                      const double Beta );

//! Lognormal proxy call price (2-moment match). Mu unused (the forward pins the
//! mean); Var is the lognormal variance from M2_to_LN. = Black-76 in (Var) form.
double LN_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Mu,
                      const double Var );

//! Lognormal proxy put price via put/call parity.
double LN_Put_Price( const double Forward,
                     const double Strike,
                     const double DiscountFactor,
                     const double Mu,
                     const double Var );

//! Shifted-lognormal proxy call price (3-moment match): the underlying is
//! D + lognormal(Mu, Var), so the shift D injects skew. Mu/Var/D come from
//! M3_to_SLN.
double SLN_Call_Price( const double Forward,
                       const double Strike,
                       const double DiscountFactor,
                       const double Mu,
                       const double Var,
                       const double D );

//! Shifted-lognormal proxy put price via put/call parity.
double SLN_Put_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Mu,
                      const double Var,
                      const double D );

//! Invert the first two raw moments (M1 mean, M2) to inverse-gamma (Alpha, Beta).
void M2_to_IG( const double M1,
               const double M2,
               double& Alpha,
               double& Beta );

//! Invert the first two raw moments to a lognormal variance (Var = ln(M2/M1^2));
//! Mu is implied by the forward and left unset here.
void M2_to_LN( const double M1,
               const double M2,
               double& Mu,
               double& Var );

//! Invert the first three raw moments to shifted-lognormal (Mu, Var, shift D).
//! Falls back to the 2-moment LN match (D=0) when the third central moment is
//! ~0 (near-symmetric basket), avoiding a divide-by-skew blow-up. Throws if the
//! implied variance is non-positive.
void M3_to_SLN( const double M1,
                const double M2,
                const double M3,
                double& Mu,
                double& Var,
                double& D );

//! Raw moments M1..M4 of a lognormal basket from per-asset forwards (Fwds),
//! lognormal vols (Vols) and the correlation matrix (Corr). Uses the closed-form
//! E[prod S_i] = prod F * exp(sum vol_i vol_j rho_ij). (M4 is currently unused.)
void LN_to_M4( la_vector* Fwds,
               la_vector* Vols,
               la_matrix* Corr,
               double& M1,
               double& M2,
               double& M3,
               double& M4 );

//! Reshape a flat la_vector into a (v->size/row_size) x row_size matrix (copy,
//! caller-owned). Row-major fill.
la_matrix* ext_la_vector_to_matrix( const la_vector* v,
                                    size_t row_size );
//! in-place lower-triangular Cholesky (A = L L^T); false if not positive-definite
[[nodiscard]] bool CholeskyDecomposeLower( la_matrix* A );
//! solve a tridiagonal system M X = B (Thomas algorithm)
void SolveTridiagonal( const la_vector* Diag, const la_vector* Super,
                       const la_vector* Sub, const la_vector* B, la_vector* X );
//! ordinary least squares (normal equations + Cholesky); returns beta (size X cols)
[[nodiscard]] vector<double> LeastSquares( const la_matrix* X, const la_vector* y );
//! true iff m is symmetric AND positive-definite (tested via a Cholesky on a copy)
[[nodiscard]] bool ext_la_matrix_is_positive( const la_matrix* m );
//! true iff m has equal row/column count
[[nodiscard]] bool ext_la_matrix_is_square( const la_matrix* m );
//! true iff m is square and equal to its transpose (exact equality)
[[nodiscard]] bool ext_la_matrix_is_symmetric( const la_matrix* m );
//! Repair a non-PD correlation matrix in place: bisection-search the smallest
//! blend eps that makes (1-eps)*m + eps*I positive-definite, and apply it. Returns
//! the chosen eps (0 if no shift was needed). See ext_la_matrix_shift_to_epsilon.
void ext_la_matrix_to_near_positive( la_matrix* m,
                                     double& eps );
//! In place: scale every OFF-diagonal entry by (1-eps), i.e. (1-eps)*m + eps*I for
//! a unit-diagonal correlation matrix — shrinks correlations toward the identity.
void ext_la_matrix_shift_to_epsilon( la_matrix* m,
                                     double eps );
//! sum of all elements of v
double ext_la_vector_sum( la_vector* v );

LaMatrix ToLaMatrix( const vector<double>& Matrix ); //!< caller-owned (RAII)
//! Flatten an la_matrix to a row-major std::vector.
vector<double> FromLaMatrix( la_matrix* Matrix );

//! Natural cubic-spline interpolation: fit a spline through (x_serie, y_serie) and
//! evaluate at x_point (clamped to the data range). Natural = zero 2nd derivative
//! at both ends (correct for a non-periodic PDE price profile). x_serie ascending.
double InterpolateWithSpline( la_vector* x_serie, la_vector* y_serie, double x_point );