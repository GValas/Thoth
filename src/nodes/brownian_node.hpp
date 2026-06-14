#pragma once
#include "monte_carlo_node.hpp"

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
