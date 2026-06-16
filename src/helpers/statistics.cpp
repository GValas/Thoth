#include "statistics.hpp"

#include <cmath>

double Sum( const double* X, std::size_t N )
{
    double s = 0.0;
    for ( std::size_t i = 0; i < N; i++ )
    {
        s += X[i];
    }
    return s;
}

double WeightedMean( const double* W, const double* X, std::size_t N )
{
    double sw = 0.0, swx = 0.0;
    for ( std::size_t i = 0; i < N; i++ )
    {
        sw += W[i];
        swx += W[i] * X[i];
    }
    return swx / sw;
}

double WeightedVarianceM( const double* W, const double* X, std::size_t N, double Mean )
{
    double sw = 0.0, sw2 = 0.0, s = 0.0;
    for ( std::size_t i = 0; i < N; i++ )
    {
        const double w = W[i];
        const double dx = X[i] - Mean;
        sw += w;
        sw2 += w * w;
        s += w * dx * dx;
    }
    //! unbiased weighting factor V1 / (V1^2 - V2), matching gsl_stats_wvariance_m
    return s * ( sw / ( sw * sw - sw2 ) );
}

double WeightedSd( const double* W, const double* X, std::size_t N )
{
    return std::sqrt( WeightedVarianceM( W, X, N, WeightedMean( W, X, N ) ) );
}
