#pragma once
#include "monte_carlo_node.hpp"
#include "rng.hpp"

//! Independent standard-normal innovation for one factor per step. Draws from its
//! own RNG, or — when a quasi-noise buffer is set — reads the (Sobol +
//! Brownian-bridge) normalized increments produced by the PathGenerator.
//!
//! Role in the MC node graph: this is a *leaf* (source) node. It has no DAG
//! children — its value for a date is a fresh N(0,1) draw, the raw randomness that
//! everything downstream is built from. A per-factor stack of these feeds the
//! CorrelatedNoiseNode (Cholesky combine) and then the BrownianNode
//! (W_i = W_{i-1} + sqrt(dt_i) * noise). Because the value is the *normalized*
//! increment dW_i / sqrt(dt_i), the quasi-noise path and the pseudo-random path
//! are interchangeable downstream.
//!
//! Invariant: exactly one of _random_generator / _quasi_noise is the active
//! source per evaluation; when _quasi_noise is wired it takes priority.
class NoiseNode : public MonteCarloNode
{

  private:
    Rng* _random_generator; //!< pseudo-random Gaussian source (used when no quasi buffer)
    //! when set, the per-path normalized increments come from the (Sobol +
    //! Brownian-bridge) PathGenerator instead of an independent pseudo-random draw.
    //! Non-owning view of one factor's buffer; indexed by diffusion date.
    const vector<double>* _quasi_noise = nullptr;

  public:
    //! fill this date's value with a standard-normal innovation: the quasi-noise
    //! buffer entry if wired, otherwise a fresh pseudo-random Gaussian draw.
    void ComputeValue( size_t DateIndex ) override;
    //! leaf node: no DAG children, so this declares no dependencies (no-op).
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetRandomGenerator( Rng* RandomGenerator ); //!< set the pseudo-random source
    //! point this node at a PathGenerator factor buffer (Sobol + Brownian bridge);
    //! pass nullptr to revert to pure pseudo-random draws.
    void SetNoiseBuffer( const vector<double>* Buffer );

    NoiseNode( const string& Name );
    ~NoiseNode() override;
};
