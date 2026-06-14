#pragma once
#include "monte_carlo_node.hpp"

class CorrelatedNoiseNode : public MonteCarloNode
{

  private:
    vector<MonteCarloNode*> _noise_node_list;
    vector<MonteCarloNode*> _cholesky_node_list;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    vector<MonteCarloNode*> GetNoiseNodeList();
    vector<MonteCarloNode*> GetCholeskyNodeList();

    //! setter
    void PushNoiseNode( MonteCarloNode* N );
    void PushCholeskyNode( MonteCarloNode* N );

    CorrelatedNoiseNode( const string& Name );
    ~CorrelatedNoiseNode() override;
};
