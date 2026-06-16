#pragma once

#include <cstddef>

//! ----------------------------------------------------------------------
//! Weighted descriptive statistics — part of the migration off GSL.
//!
//! These operate on plain contiguous double arrays (the callers still hold the
//! data in la_vector for now, passing la_vector_ptr). Conventions match the
//! gsl_stats_w* routines they replace: the weighted variance is the unbiased
//! estimator with the V1/(V1^2 - V2) bias factor (V1 = sum w, V2 = sum w^2).
//! ----------------------------------------------------------------------

//! plain sum of N elements
double Sum( const double* X, std::size_t N );

//! weighted mean  (sum w_i x_i) / (sum w_i)            == gsl_stats_wmean
double WeightedMean( const double* W, const double* X, std::size_t N );

//! unbiased weighted variance about a known mean       == gsl_stats_wvariance_m
double WeightedVarianceM( const double* W, const double* X, std::size_t N, double Mean );

//! weighted standard deviation about the (internally computed) weighted mean
//!                                                      == gsl_stats_wsd
double WeightedSd( const double* W, const double* X, std::size_t N );
