#pragma once
#include "monte_carlo_node.hpp"

class NoiseNode : public MonteCarloNode
{

  private:
    gsl_rng* _random_generator;
    //! when set, the per-path normalized increments come from the (Sobol +
    //! Brownian-bridge) PathGenerator instead of an independent pseudo-random draw
    const vector<double>* _quasi_noise = nullptr;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetRandomGenerator( gsl_rng* RandomGenerator );
    void SetNoiseBuffer( const vector<double>* Buffer );

    NoiseNode( const string& Name );
    ~NoiseNode() override;
};
