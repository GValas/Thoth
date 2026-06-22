#pragma once
#include "monte_carlo_node.hpp"

//! Cumulative Brownian path: W_i = W_{i-1} + sqrt(dt_i) * noise_i, accumulated from
//! a standard-normal noise node over the diffusion dates.
class BrownianNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _noise_node = nullptr; //!< standard-normal increment source (one N(0,1) per step)

  public:
    //! true only at date 0 (where W=0 is a fixed constant, evaluated once)
    bool IsConstant( size_t DateIndex ) override;
    //! accumulate W_i = W_{i-1} + sqrt(dt_i) * noise_i along the path
    void ComputeValue( size_t DateIndex ) override;
    //! each step depends on the noise at this date and on W at the previous date
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetNoiseNode( MonteCarloNode* NoiseNode ); //!< wire the driving N(0,1) noise node

    //! getter

    BrownianNode( const string& Name );
    ~BrownianNode() override;
};
