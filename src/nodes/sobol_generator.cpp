#include "thoth.hpp"
#include "sobol_generator.hpp"

namespace
{
//! 1 / 2^32, scales a 32-bit integer coordinate into [0, 1)
constexpr double SCALE = 1.0 / 4294967296.0;
} // namespace

//! Highest dimension count the embedded Joe-Kuo dataset can drive: the FILE_DIMS
//! tabulated dimensions plus the implicit first dimension built without a table.
unsigned SobolGenerator::MaxDimension()
{
    return sobol_jk::FILE_DIMS + 1; //!< +1 for the implicit first dimension
}

//! Build the direction-number tables for the first Dimension dimensions (clamped to
//! MaxDimension). For each dimension d this fills _v[d][0..BITS-1], the 32-bit
//! direction numbers that the Gray-code recurrence XORs in Next(). All coordinates
//! _x start at 0 (the skipped all-zero point); the first real point is drawn on the
//! first Next().
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
            //! seed the first s direction numbers from the tabulated m_1..m_s
            //! (m_i is an odd integer < 2^i; shifting left aligns it to the top bit).
            for ( unsigned i = 0; i < s; i++ )
            {
                V[i] = (uint32_t)m[i] << ( BITS - 1 - i );
            }
            //! extend with the primitive-polynomial recurrence: each further V is the
            //! XOR of V[i-s] (also shifted down by s) with the V[i-k] selected by the
            //! polynomial coefficient bits packed in A.
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

//! Produce the next quasi-random point in [0,1)^dim. Side effect: advances _x and
//! increments _count. Resizes Point to _dim and writes each coordinate.
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

    //! one XOR per dimension with the flipped direction number, then scale the
    //! 32-bit integer coordinate down into [0,1).
    for ( unsigned d = 0; d < _dim; d++ )
    {
        _x[d] ^= _v[d][c];
        Point[d] = _x[d] * SCALE;
    }
    _count++;
}

//! Discard the next Count points, advancing the integer state without scaling or
//! emitting coordinates. Side effect: advances _x and _count by Count. Used so each
//! cluster slave consumes a disjoint block of the same Sobol sequence.
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
