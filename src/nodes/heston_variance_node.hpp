#pragma once
#include "monte_carlo_node.hpp"

//! CIR variance process of the Heston model, integrated with Andersen's
//! Quadratic-Exponential (QE) scheme — non-negative and low-bias, far better
//! than an Euler step on sqrt(v). Driven by one Gaussian noise per step (the
//! exponential branch turns it into a uniform via the normal CDF).
class HestonVarianceNode : public MonteCarloNode
{
  private:
    double _v0 = 0;
    double _kappa = 0;
    double _theta = 0;
    double _xi = 0;
    MonteCarloNode* _noise_node = nullptr; //!< independent N(0,1) per step

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    void SetParameters( double V0, double Kappa, double Theta, double Xi );
    void SetNoiseNode( MonteCarloNode* N ) { _noise_node = N; }

    HestonVarianceNode( const string& Name );
    ~HestonVarianceNode() override;
};
