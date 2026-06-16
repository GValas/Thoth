#pragma once
#include "thoth.hpp"
#include "rng.hpp"
#include <memory>

class SobolGenerator; //!< Joe-Kuo direction-number Sobol sequence

//! Per-path Gaussian generator for the Monte-Carlo engine, with optional Sobol
//! quasi-randoms and a Brownian bridge.
//!
//! For each path it builds, for every factor (one per diffused underlying), a
//! Brownian path over the diffusion dates using the *Brownian bridge*: the
//! endpoint is set first, then successive midpoints. This puts the coarsest,
//! most important structure in the lowest dimensions, which are drawn from a
//! Joe-Kuo Sobol low-discrepancy sequence (up to several thousand dimensions);
//! the remaining fine increments fall back to pseudo-random draws. The result
//! is exposed as the *normalized* increment n[factor][i] = dW_i / sqrt(dt_i), so
//! the existing BrownianNode (W_i = W_{i-1} + sqrt(dt_i) * noise) reproduces the
//! bridge path unchanged, and correlation is still applied downstream by the
//! Cholesky combine.
class PathGenerator
{

  public:
    //! Times = year-fractions of the diffusion dates (Times[0] = 0 = today).
    //! SobolSkip discards that many leading Sobol points (one per path), so a
    //! cluster slave can draw a disjoint block of the low-discrepancy sequence.
    PathGenerator( const vector<double>& Times, int Factors, bool UseSobol, Rng* RandomGenerator,
                   uint64_t SobolSkip = 0 );
    ~PathGenerator();

    //! draw the next path (refills the per-factor normalized-increment buffers)
    void NextPath();

    //! buffer of normalized increments for a factor (index by diffusion date)
    const vector<double>* Buffer( int Factor ) const { return &_noise[Factor]; }

  private:
    vector<double> _t; //!< diffusion year-fractions, _t[0] = 0
    int _m;            //!< number of steps (bridge points), = _t.size() - 1
    int _factors;
    bool _use_sobol;
    Rng* _rng; //!< pseudo-random source (tail dimensions / non-sobol)

    //! Sobol sequence over the first _sobol_dim global dimensions
    std::unique_ptr<SobolGenerator> _sobol;
    int _sobol_dim = 0;

    //! Brownian bridge schedule (Glasserman / QuantLib construction), size _m
    vector<int> _bridge_index, _left_index, _right_index;
    vector<double> _left_weight, _right_weight, _std_dev;

    //! per-factor normalized increments, [_factors][_m + 1] (index 0 unused)
    vector<vector<double>> _noise;

    //! scratch
    vector<double> _u;         //!< sobol uniforms
    vector<vector<double>> _z; //!< per-factor standard normals over bridge steps
    vector<double> _w;         //!< one factor's bridge path

    void BuildBridgeSchedule();
};
