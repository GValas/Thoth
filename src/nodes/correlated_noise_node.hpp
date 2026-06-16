#pragma once
#include "monte_carlo_node.hpp"

//! Correlated Gaussian increment for one factor: sum_i noise_i * L_i, the dot
//! product of the independent noise nodes with a row of the correlation matrix's
//! Cholesky factor (supplied as constant Cholesky nodes).
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
