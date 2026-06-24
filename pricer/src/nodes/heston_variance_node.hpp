#pragma once
#include "monte_carlo_node.hpp"

//! CIR variance process of the Heston model, integrated with Andersen's
//! Quadratic-Exponential (QE) scheme — non-negative and low-bias, far better
//! than an Euler step on sqrt(v). Driven by one Gaussian noise per step (the
//! exponential branch turns it into a uniform via the normal CDF).
class HestonVarianceNode : public MonteCarloNode
{
  private:
    double _v0 = 0;                        //!< initial variance v(0)
    double _kappa = 0;                     //!< mean-reversion speed
    double _theta = 0;                     //!< long-run variance
    double _xi = 0;                        //!< vol-of-vol
    MonteCarloNode* _noise_node = nullptr; //!< independent N(0,1) per step

  public:
    //! one QE step: v_i from v_{i-1} via the quadratic or exponential branch
    void ComputeValue( size_t DateIndex ) override;
    //! depends on its own previous variance and the current noise; date 0 has none
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    void SetParameters( double V0, double Kappa, double Theta, double Xi ); //!< CIR params + v(0)
    void SetNoiseNode( MonteCarloNode* N ) { _noise_node = N; }             //!< wire the driving N(0,1) noise

    HestonVarianceNode( const string& Name );
    ~HestonVarianceNode() override;
};
