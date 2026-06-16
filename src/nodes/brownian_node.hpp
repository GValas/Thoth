#pragma once
#include "monte_carlo_node.hpp"

//! Cumulative Brownian path: W_i = W_{i-1} + sqrt(dt_i) * noise_i, accumulated from
//! a standard-normal noise node over the diffusion dates.
class BrownianNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _noise_node = nullptr;

  public:
    bool IsConstant( size_t DateIndex ) override;
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetNoiseNode( MonteCarloNode* NoiseNode );

    //! getter
    MonteCarloNode* GetNoiseNode();

    BrownianNode( const string& Name );
    ~BrownianNode() override;
};
