#include "thoth.hpp"
#include "path_generator.hpp"
#include "sobol_generator.hpp"
#include "distributions.hpp"

//! Construct the path generator for a fixed diffusion schedule. Builds the
//! Brownian-bridge schedule once, sizes the Sobol generator (capped at the
//! embedded dimension count), pre-skips the requested cluster offset, and
//! allocates the per-factor scratch buffers reused on every NextPath().
//! _m = number of bridge points = (#diffusion dates) - 1, since date 0 = today
//! carries W = 0 and is not a bridge point.
PathGenerator::PathGenerator( const vector<double>& Times, int Factors, bool UseSobol, Rng* RandomGenerator,
                              uint64_t SobolSkip )
    : _t( Times ), _m( (int)Times.size() - 1 ), _factors( Factors ), _use_sobol( UseSobol ), _rng( RandomGenerator )
{
    BuildBridgeSchedule();

    //! the first dimensions (bridge endpoints/midpoints of every factor) get the
    //! Sobol treatment; cap at the embedded Joe-Kuo dimension count, beyond which
    //! the (least important) fine increments fall back to pseudo-random draws.
    if ( _use_sobol && _m > 0 )
    {
        long total = (long)_m * _factors;
        _sobol_dim = (int)min<long>( total, SobolGenerator::MaxDimension() );
        _sobol = make_unique<SobolGenerator>( _sobol_dim );
        if ( SobolSkip > 0 )
        {
            _sobol->Skip( SobolSkip ); //!< disjoint Sobol block for this cluster slave
        }
    }

    //! reusable buffers (allocated once, refilled each path): _noise[f] indexed by
    //! diffusion date with index 0 unused (W=0 there); _z[f] the standard normals
    //! over the _m bridge steps; _u the Sobol uniforms; _w one factor's bridge path.
    _noise.assign( _factors, vector<double>( _m + 1, 0.0 ) );
    _z.assign( _factors, vector<double>( _m, 0.0 ) );
    _u.assign( _sobol_dim, 0.0 );
    _w.assign( _m, 0.0 );
}

PathGenerator::~PathGenerator() = default;

//! QuantLib-style Brownian bridge schedule over the _m points at times _t[1.._m].
//! Precomputes, for each build step i, which point is being set (_bridge_index),
//! its already-set left/right anchors (_left_index/_right_index), the linear
//! interpolation weights (_left_weight/_right_weight) and the conditional standard
//! deviation (_std_dev) of the bridge increment. Pure function of the time grid,
//! so it is path-independent and run once. No-op when there are no steps.
void PathGenerator::BuildBridgeSchedule()
{
    _bridge_index.assign( _m, 0 );
    _left_index.assign( _m, 0 );
    _right_index.assign( _m, 0 );
    _left_weight.assign( _m, 0.0 );
    _right_weight.assign( _m, 0.0 );
    _std_dev.assign( _m, 0.0 );
    if ( _m == 0 )
    {
        return;
    }

    //! bridge point k is at time _t[k+1] (k = 0 .. _m-1)
    auto bt = [&]( int k )
    { return _t[k + 1]; };

    vector<int> map( _m, 0 ); //!< 0 = not yet set, else 1-based build order
    //! step 0 fixes the endpoint (the most important dimension) directly from a
    //! single normal: W(T) = sqrt(T) * Z, no left/right anchors.
    map[_m - 1] = 1;
    _bridge_index[0] = _m - 1;
    _std_dev[0] = sqrt( bt( _m - 1 ) ); //!< endpoint: W = sqrt(T) * Z
    _left_weight[0] = _right_weight[0] = 0.0;

    //! subsequent steps insert the midpoint of the largest not-yet-filled gap, so
    //! the coarse structure of the path is decided by the low Sobol dimensions and
    //! the fine detail by the high ones (variance front-loading).
    int j = 0;
    for ( int i = 1; i < _m; i++ )
    {
        while ( map[j] )
        {
            j++; //!< first unset point
        }
        int k = j;
        while ( !map[k] )
        {
            k++; //!< next set point to the right (the gap's right anchor)
        }
        int l = j + ( ( k - 1 - j ) >> 1 ); //!< midpoint to set now
        map[l] = i + 1;
        _bridge_index[i] = l;
        _left_index[i] = j;
        _right_index[i] = k;
        if ( j != 0 )
        {
            //! interior gap: condition on both anchors W(j-1) and W(k). Mean is the
            //! time-linear interpolation; variance is the Brownian-bridge conditional
            //! variance (t_l - t_{j-1})(t_k - t_l)/(t_k - t_{j-1}).
            _left_weight[i] = ( bt( k ) - bt( l ) ) / ( bt( k ) - bt( j - 1 ) );
            _right_weight[i] = ( bt( l ) - bt( j - 1 ) ) / ( bt( k ) - bt( j - 1 ) );
            _std_dev[i] = sqrt( ( bt( l ) - bt( j - 1 ) ) * ( bt( k ) - bt( l ) ) / ( bt( k ) - bt( j - 1 ) ) );
        }
        else
        {
            //! left edge (j == 0): no left anchor since W(0) = 0 at today, so the
            //! mean uses only the right anchor and the variance reduces to
            //! t_l (t_k - t_l)/t_k.
            _left_weight[i] = ( bt( k ) - bt( l ) ) / bt( k );
            _right_weight[i] = bt( l ) / bt( k );
            _std_dev[i] = sqrt( bt( l ) * ( bt( k ) - bt( l ) ) / bt( k ) );
        }
        //! continue scanning past the just-closed right anchor; wrap to restart the
        //! left-to-right sweep for the next round of gaps.
        j = k + 1;
        if ( j >= _m )
        {
            j = 0;
        }
    }
}

//! Advance to the next path: draw a fresh block of standard normals, run the
//! Brownian bridge for every factor, and refill the _noise buffers with the
//! normalized increments the NoiseNodes read. Side effect: mutates _u, _z, _w and
//! _noise, and advances the Sobol/pseudo-random streams. No-op for an empty grid.
void PathGenerator::NextPath()
{
    if ( _m == 0 )
    {
        return;
    }

    //! draw the standard normals : Sobol (inverse-CDF) on the low dimensions,
    //! pseudo-random on the tail. Global dim = step*factors + factor, so every
    //! factor's endpoint (step 0) lands in the lowest Sobol dimensions.
    if ( _sobol )
    {
        _sobol->Next( _u );
    }
    for ( int step = 0; step < _m; step++ )
    {
        for ( int f = 0; f < _factors; f++ )
        {
            long dim = (long)step * _factors + f;
            double z;
            if ( dim < _sobol_dim )
            {
                double u = min( 1.0 - 1e-12, max( 1e-12, _u[dim] ) );
                z = NormalCdfInv( u );
            }
            else
            {
                z = _rng->Gaussian();
            }
            _z[f][step] = z;
        }
    }

    //! build each factor's Brownian bridge path, then its normalized increments
    for ( int f = 0; f < _factors; f++ )
    {
        const vector<double>& z = _z[f];
        //! step 0: place the endpoint W(T) = sqrt(T) * Z directly.
        _w[_bridge_index[0]] = _std_dev[0] * z[0];
        for ( int i = 1; i < _m; i++ )
        {
            int li = _left_index[i];
            int ri = _right_index[i];
            int bi = _bridge_index[i];
            //! conditional draw: mean = interpolation of the two anchors, plus the
            //! independent bridge increment _std_dev[i] * z[i]. The li != 0 / else
            //! split mirrors the interior-gap vs left-edge cases of the schedule
            //! (left anchor is W[li-1] because _w is 0-based over points 1.._m).
            if ( li != 0 )
            {
                _w[bi] = _left_weight[i] * _w[li - 1] + _right_weight[i] * _w[ri] + _std_dev[i] * z[i];
            }
            else
            {
                _w[bi] = _right_weight[i] * _w[ri] + _std_dev[i] * z[i];
            }
        }

        //! convert the bridge path W into the normalized increments the downstream
        //! BrownianNode expects: n[i] = (W_i - W_{i-1}) / sqrt(dt_i). W at diffusion
        //! index 0 is 0; _w[k] holds W at diffusion index k+1. A zero-length step
        //! yields 0 to avoid a divide-by-zero on coincident dates.
        vector<double>& n = _noise[f];
        double prev = 0.0;
        for ( int i = 1; i <= _m; i++ )
        {
            double w = _w[i - 1];
            double dt = _t[i] - _t[i - 1];
            n[i] = ( dt > 0 ) ? ( w - prev ) / sqrt( dt ) : 0.0;
            prev = w;
        }
    }
}
