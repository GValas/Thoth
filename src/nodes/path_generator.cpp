#include "thoth.hpp"
#include "path_generator.hpp"
#include "sobol_generator.hpp"
#include "distributions.hpp"

PathGenerator::PathGenerator( const vector<double>& Times, int Factors, bool UseSobol, gsl_rng* Rng,
                              uint64_t SobolSkip )
    : _t( Times ), _m( (int)Times.size() - 1 ), _factors( Factors ), _use_sobol( UseSobol ), _rng( Rng )
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

    _noise.assign( _factors, vector<double>( _m + 1, 0.0 ) );
    _z.assign( _factors, vector<double>( _m, 0.0 ) );
    _u.assign( _sobol_dim, 0.0 );
    _w.assign( _m, 0.0 );
}

PathGenerator::~PathGenerator() = default;

//! QuantLib-style Brownian bridge schedule over the _m points at times _t[1.._m].
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
    map[_m - 1] = 1;
    _bridge_index[0] = _m - 1;
    _std_dev[0] = sqrt( bt( _m - 1 ) ); //!< endpoint: W = sqrt(T) * Z
    _left_weight[0] = _right_weight[0] = 0.0;

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
            k++; //!< next set point to the right
        }
        int l = j + ( ( k - 1 - j ) >> 1 ); //!< midpoint to set now
        map[l] = i + 1;
        _bridge_index[i] = l;
        _left_index[i] = j;
        _right_index[i] = k;
        if ( j != 0 )
        {
            _left_weight[i] = ( bt( k ) - bt( l ) ) / ( bt( k ) - bt( j - 1 ) );
            _right_weight[i] = ( bt( l ) - bt( j - 1 ) ) / ( bt( k ) - bt( j - 1 ) );
            _std_dev[i] = sqrt( ( bt( l ) - bt( j - 1 ) ) * ( bt( k ) - bt( l ) ) / ( bt( k ) - bt( j - 1 ) ) );
        }
        else
        {
            _left_weight[i] = ( bt( k ) - bt( l ) ) / bt( k );
            _right_weight[i] = bt( l ) / bt( k );
            _std_dev[i] = sqrt( bt( l ) * ( bt( k ) - bt( l ) ) / bt( k ) );
        }
        j = k + 1;
        if ( j >= _m )
        {
            j = 0;
        }
    }
}

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
                z = gsl_ran_gaussian_ziggurat( _rng, 1.0 );
            }
            _z[f][step] = z;
        }
    }

    //! build each factor's Brownian bridge path, then its normalized increments
    for ( int f = 0; f < _factors; f++ )
    {
        const vector<double>& z = _z[f];
        _w[_bridge_index[0]] = _std_dev[0] * z[0];
        for ( int i = 1; i < _m; i++ )
        {
            int li = _left_index[i];
            int ri = _right_index[i];
            int bi = _bridge_index[i];
            if ( li != 0 )
            {
                _w[bi] = _left_weight[i] * _w[li - 1] + _right_weight[i] * _w[ri] + _std_dev[i] * z[i];
            }
            else
            {
                _w[bi] = _right_weight[i] * _w[ri] + _std_dev[i] * z[i];
            }
        }

        //! W at diffusion index 0 is 0; _w[k] is W at diffusion index k+1
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
