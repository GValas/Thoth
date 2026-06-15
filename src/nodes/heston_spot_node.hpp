#pragma once
#include "monte_carlo_node.hpp"

//! Heston spot diffusion using Andersen's (2008) log-spot scheme: the spot/
//! variance correlation rho is folded into the K0..K2 drift coefficients (via
//! the variance increment), and the residual is an independent Gaussian. Reads
//! the variance node at this and the previous date, the carry (drift) node, and
//! one independent N(0,1) noise.
class HestonSpotNode : public MonteCarloNode
{
  private:
    double _spot = 0;
    double _kappa = 0;
    double _theta = 0;
    double _xi = 0;
    double _rho = 0;
    MonteCarloNode* _variance_node = nullptr;
    MonteCarloNode* _drift_node = nullptr;
    MonteCarloNode* _noise_node = nullptr;
    MonteCarloNode* _jump_node = nullptr; //!< optional Bates jump increment (null = pure Heston)

  public:
    bool IsConstant( size_t DateIndex ) override;
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    void SetParameters( double Kappa, double Theta, double Xi, double Rho );
    void SetVarianceNode( MonteCarloNode* N ) { _variance_node = N; }
    void SetDriftNode( MonteCarloNode* N ) { _drift_node = N; }
    void SetNoiseNode( MonteCarloNode* N ) { _noise_node = N; }
    void SetJumpNode( MonteCarloNode* N ) { _jump_node = N; }
    void SetSpot( double Spot );

    HestonSpotNode( const string& Name );
    ~HestonSpotNode() override;
};
