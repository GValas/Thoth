#pragma once

#include <cstdint>

#include "distributions.hpp" //!< NormalCdfInv (Acklam) for the Gaussian transform

//! ----------------------------------------------------------------------
//! Pseudo-random engine for the Monte-Carlo path generator — replaces the GSL
//! gsl_rng + ziggurat (part of the GSL-removal migration).
//!
//! Uses xoshiro256++ (Blackman & Vigna, public domain): tiny, fast and a good
//! statistical quality for Monte-Carlo. Seeded from a single 64-bit value via
//! splitmix64. It exposes the standard UniformRandomBitGenerator interface
//! (result_type / min / max / operator()), so it can also drive a
//! std::*_distribution (the jump node uses std::poisson_distribution). Gaussian
//! draws go through the inverse-CDF (NormalCdfInv) — see distributions.hpp.
//! ----------------------------------------------------------------------
class Rng
{
    std::uint64_t _s[4]; //!< 256-bit xoshiro state (must not be all-zero)

    //! 64-bit left bit-rotation by K (the xoshiro scrambler primitive)
    static inline std::uint64_t Rotl( std::uint64_t X, int K )
    {
        return ( X << K ) | ( X >> ( 64 - K ) );
    }

  public:
    using result_type = std::uint64_t;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return UINT64_MAX; }

    explicit Rng( std::uint64_t Seed = 0 ) { this->Seed( Seed ); }

    //! splitmix64: expand a single seed into the 256-bit state (recommended seeding)
    void Seed( std::uint64_t Seed )
    {
        std::uint64_t z = Seed;
        for ( int i = 0; i < 4; i++ )
        {
            z += 0x9e3779b97f4a7c15ULL;
            std::uint64_t x = z;
            x = ( x ^ ( x >> 30 ) ) * 0xbf58476d1ce4e5b9ULL;
            x = ( x ^ ( x >> 27 ) ) * 0x94d049bb133111ebULL;
            _s[i] = x ^ ( x >> 31 );
        }
    }

    //! next 64 random bits (xoshiro256++)
    result_type operator()()
    {
        const std::uint64_t result = Rotl( _s[0] + _s[3], 23 ) + _s[0];
        const std::uint64_t t = _s[1] << 17;
        _s[2] ^= _s[0];
        _s[3] ^= _s[1];
        _s[1] ^= _s[2];
        _s[0] ^= _s[3];
        _s[2] ^= t;
        _s[3] = Rotl( _s[3], 45 );
        return result;
    }

    //! uniform double in the OPEN interval (0,1) — 53-bit resolution, never 0 or 1
    //! (so the inverse-CDF below never sees a pole)
    double Uniform()
    {
        const double u = ( ( *this )() >> 11 ) * ( 1.0 / 9007199254740992.0 ); //!< [0,1)
        return u > 0.0 ? u : ( 1.0 / 9007199254740992.0 );
    }

    //! standard normal draw N(0,1) via the inverse-CDF
    double Gaussian() { return NormalCdfInv( Uniform() ); }
};
