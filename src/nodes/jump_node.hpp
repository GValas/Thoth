#pragma once
#include "monte_carlo_node.hpp"
#include "rng.hpp"

#include <random>

//! Bates compound-Poisson jump increment for one diffusion step, in log-spot
//! space: the drift compensator -lambda*kbar*dt plus the realised jump. Over a
//! step the number of jumps is Poisson(lambda*dt) and the aggregate log jump of
//! n lognormal jumps N(mu, sigma^2) is N(n*mu, n*sigma^2). kbar = E[e^J-1] =
//! exp(mu + sigma^2/2) - 1 keeps the spot a martingale. Owns its own RNG (an
//! independent source from the Brownian noise), like NoiseNode.
class JumpNode : public MonteCarloNode
{
  private:
    Rng* _rng = nullptr;
    double _lambda = 0; //!< jump intensity (per year)
    double _mu = 0;     //!< mean of the log jump size
    double _sigma = 0;  //!< vol of the log jump size
    double _kbar = 0;   //!< E[e^J - 1] compensator

    //! one Poisson(lambda*dt) per diffusion step, built once and reused across paths
    //! (dt is per-date, not per-path) instead of reconstructed on every ComputeValue
    vector<std::poisson_distribution<unsigned int>> _poisson;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setters
    void SetRandomGenerator( Rng* RandomGenerator );
    void SetJumpParameters( double Lambda, double Mu, double Sigma );

    JumpNode( const string& Name );
    ~JumpNode() override;
};
