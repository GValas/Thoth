#pragma once
#include "thoth.hpp"
#include <cstdint>

//! Embedded Joe-Kuo direction-number dataset (see sobol_direction_numbers.cpp).
namespace sobol_jk
{
extern const unsigned FILE_DIMS;  //!< number of embedded file dimensions
extern const unsigned S[];        //!< primitive-polynomial degrees
extern const unsigned A[];        //!< polynomial coefficients (packed bits)
extern const unsigned M_OFFSET[]; //!< prefix offsets into M, size FILE_DIMS+1
extern const unsigned M[];        //!< initial direction numbers m_1..m_s
} // namespace sobol_jk

//! Joe-Kuo Sobol low-discrepancy generator.
//!
//! Builds the per-dimension direction numbers from the embedded Joe-Kuo dataset
//! (primitive polynomial + initial values) and iterates with the Gray-code
//! recurrence, so successive points differ in a single dimension's coordinate.
//! Unlike GSL's Sobol generator (capped at 40 dimensions) this supports up to
//! MaxDimension() dimensions, lifting the dimension limit of the Brownian-bridge
//! Monte-Carlo path generator. The all-zero initial point is skipped.
class SobolGenerator
{

  public:
    //! a generator over the first Dimension dimensions (clamped to MaxDimension)
    explicit SobolGenerator( unsigned Dimension );

    //! fill Point[0 .. Dimension()-1] with the next quasi-random point in (0,1)
    void Next( vector<double>& Point );

    //! discard the next Count points (advance the sequence); used to give each
    //! cluster slave a disjoint block of the Sobol sequence
    void Skip( uint64_t Count );

    unsigned Dimension() const { return _dim; }

    //! highest dimension count the embedded dataset supports
    static unsigned MaxDimension();

  private:
    static constexpr unsigned BITS = 32; //!< direction numbers are 32-bit

    unsigned _dim;
    uint64_t _count = 0;         //!< number of points already drawn
    vector<vector<uint32_t>> _v; //!< direction numbers [_dim][BITS]
    vector<uint32_t> _x;         //!< current integer coordinates per dimension
};
