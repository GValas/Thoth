#include "thoth.hpp"
#include "sobol_generator.hpp"

namespace
{
//! 1 / 2^32, scales a 32-bit integer coordinate into [0, 1)
constexpr double SCALE = 1.0 / 4294967296.0;
} // namespace

unsigned SobolGenerator::MaxDimension()
{
    return sobol_jk::FILE_DIMS + 1; //!< +1 for the implicit first dimension
}

SobolGenerator::SobolGenerator( unsigned Dimension ) : _dim( Dimension )
{
    if ( _dim > MaxDimension() )
    {
        _dim = MaxDimension();
    }
    _v.assign( _dim, vector<uint32_t>( BITS, 0 ) );
    _x.assign( _dim, 0 );

    //! first dimension: all initial direction numbers are one, V[i] = 1 << (31-i)
    for ( unsigned i = 0; i < BITS; i++ )
    {
        _v[0][i] = 1u << ( BITS - 1 - i );
    }

    //! remaining dimensions: build direction numbers from the Joe-Kuo dataset.
    //! internal dimension d (>= 1) maps to file entry d-1 (file dimension d+1).
    for ( unsigned d = 1; d < _dim; d++ )
    {
        unsigned idx = d - 1;
        unsigned s = sobol_jk::S[idx];
        unsigned a = sobol_jk::A[idx];
        const unsigned* m = &sobol_jk::M[sobol_jk::M_OFFSET[idx]];
        vector<uint32_t>& V = _v[d];

        if ( BITS <= s )
        {
            //! polynomial longer than the word: seed every bit from m directly
            for ( unsigned i = 0; i < BITS; i++ )
            {
                V[i] = (uint32_t)m[i] << ( BITS - 1 - i );
            }
        }
        else
        {
            for ( unsigned i = 0; i < s; i++ )
            {
                V[i] = (uint32_t)m[i] << ( BITS - 1 - i );
            }
            for ( unsigned i = s; i < BITS; i++ )
            {
                V[i] = V[i - s] ^ ( V[i - s] >> s );
                for ( unsigned k = 1; k < s; k++ )
                {
                    V[i] ^= ( ( a >> ( s - 1 - k ) ) & 1u ) * V[i - k];
                }
            }
        }
    }
}

void SobolGenerator::Next( vector<double>& Point )
{
    Point.resize( _dim );

    //! Gray-code recurrence: flip the single direction number indexed by the
    //! position of the rightmost zero bit of the current count.
    unsigned c = 0;
    for ( uint64_t value = _count; value & 1; value >>= 1 )
    {
        c++;
    }

    for ( unsigned d = 0; d < _dim; d++ )
    {
        _x[d] ^= _v[d][c];
        Point[d] = _x[d] * SCALE;
    }
    _count++;
}

void SobolGenerator::Skip( uint64_t Count )
{
    //! advance the Gray-code state without materialising the points
    for ( uint64_t n = 0; n < Count; n++ )
    {
        unsigned c = 0;
        for ( uint64_t value = _count; value & 1; value >>= 1 )
        {
            c++;
        }
        for ( unsigned d = 0; d < _dim; d++ )
        {
            _x[d] ^= _v[d][c];
        }
        _count++;
    }
}
