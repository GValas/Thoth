//! CUDA implementation of the GPU Monte-Carlo backend (see mcl_gpu.hpp).
//! Built only when THOTH_ENABLE_CUDA=ON (replaces mcl_gpu_stub.cpp).
//!
//! NOTE: this translation unit is compiled by nvcc and CANNOT be built in the
//! CPU-only devcontainer (no CUDA toolkit / GPU). It is exercised on the RTX host.
//! Kept deliberately simple and self-contained (only <string> from the project
//! interface) so nvcc never sees the C++23 engine headers.
#include "mcl_gpu.hpp"

#include <cuda_runtime.h>
#include <curand_kernel.h>

#include <cmath>
#include <string>

namespace
{

constexpr int BLOCK = 256; //!< threads per block (power of two for the reduction)

//! one thread simulates a grid-stride slice of the paths, accumulating the payoff
//! sum and sum-of-squares; a per-block shared-memory reduction then atomic-adds
//! the block partials into the two global accumulators.
//!   a = -0.5 * vol^2 * T   (log-drift)      b = vol * sqrt(T)   (log-vol)
__global__ void gbm_kernel( double forward, double strike, double a, double b,
                            int is_call, long paths, unsigned long seed,
                            double* g_sum, double* g_sum2 )
{
    const int tid = blockIdx.x * blockDim.x + threadIdx.x;
    const long stride = (long)blockDim.x * gridDim.x;

    //! counter-based RNG: cheap to init, independent per thread. Same (seed, grid)
    //! => identical draws => common random numbers across base and bumped re-prices.
    curandStatePhilox4_32_10_t st;
    curand_init( seed, (unsigned long long)tid, 0ULL, &st );

    double local_sum = 0.0;
    double local_sum2 = 0.0;
    for ( long i = tid; i < paths; i += stride )
    {
        const double z = curand_normal_double( &st );
        const double f_t = forward * exp( a + b * z );
        const double payoff = is_call ? fmax( f_t - strike, 0.0 ) : fmax( strike - f_t, 0.0 );
        local_sum += payoff;
        local_sum2 += payoff * payoff;
    }

    __shared__ double s_sum[BLOCK];
    __shared__ double s_sum2[BLOCK];
    const int t = threadIdx.x;
    s_sum[t] = local_sum;
    s_sum2[t] = local_sum2;
    __syncthreads();

    for ( int off = blockDim.x >> 1; off > 0; off >>= 1 )
    {
        if ( t < off )
        {
            s_sum[t] += s_sum[t + off];
            s_sum2[t] += s_sum2[t + off];
        }
        __syncthreads();
    }

    if ( t == 0 )
    {
        atomicAdd( g_sum, s_sum[0] );   //!< double atomics: needs sm_60+ (Ada is sm_89)
        atomicAdd( g_sum2, s_sum2[0] );
    }
}

} // namespace

namespace gpu
{

bool Available()
{
    int n = 0;
    const cudaError_t e = cudaGetDeviceCount( &n );
    return e == cudaSuccess && n > 0;
}

std::string DeviceInfo()
{
    int n = 0;
    if ( cudaGetDeviceCount( &n ) != cudaSuccess || n == 0 )
    {
        return "no CUDA device found";
    }
    cudaDeviceProp p;
    if ( cudaGetDeviceProperties( &p, 0 ) != cudaSuccess )
    {
        return "CUDA device query failed";
    }
    return std::string( p.name ) + " (sm_" + std::to_string( p.major ) + std::to_string( p.minor ) +
           ", " + std::to_string( p.multiProcessorCount ) + " SMs)";
}

GbmResult PriceEuropeanGbm( double Forward, double Strike, double T, double Vol,
                            double Df, bool IsCall, long Paths, unsigned long Seed )
{
    if ( Paths <= 0 )
    {
        return GbmResult{ 0.0, 0.0 };
    }

    const double a = -0.5 * Vol * Vol * T;
    const double b = Vol * sqrt( T );

    double* d_sum = nullptr;
    double* d_sum2 = nullptr;
    cudaMalloc( &d_sum, sizeof( double ) );
    cudaMalloc( &d_sum2, sizeof( double ) );
    cudaMemset( d_sum, 0, sizeof( double ) );
    cudaMemset( d_sum2, 0, sizeof( double ) );

    //! size the grid to the device: a few resident blocks per SM, then let the
    //! grid-stride loop cover all paths regardless of how many there are.
    int device = 0;
    cudaGetDevice( &device );
    cudaDeviceProp prop;
    cudaGetDeviceProperties( &prop, device );
    int blocks = prop.multiProcessorCount * 32;
    if ( blocks < 1 )
    {
        blocks = 1;
    }

    gbm_kernel<<<blocks, BLOCK>>>( Forward, Strike, a, b, IsCall ? 1 : 0, Paths, Seed, d_sum, d_sum2 );
    cudaDeviceSynchronize();

    double h_sum = 0.0;
    double h_sum2 = 0.0;
    cudaMemcpy( &h_sum, d_sum, sizeof( double ), cudaMemcpyDeviceToHost );
    cudaMemcpy( &h_sum2, d_sum2, sizeof( double ), cudaMemcpyDeviceToHost );
    cudaFree( d_sum );
    cudaFree( d_sum2 );

    const double n = (double)Paths;
    const double mean = h_sum / n;
    double var = h_sum2 / n - mean * mean;
    if ( var < 0.0 )
    {
        var = 0.0; //!< guard against tiny negative from round-off
    }

    GbmResult r;
    r.premium = Df * mean;
    r.trust = Df * sqrt( var / n );
    return r;
}

} // namespace gpu
