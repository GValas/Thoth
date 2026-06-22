#pragma once
#include "monte_carlo_node.hpp"

//! Correlated Gaussian increment for one factor: sum_i noise_i * L_i, the dot
//! product of the independent noise nodes with a row of the correlation matrix's
//! Cholesky factor (supplied as constant Cholesky nodes).
class CorrelatedNoiseNode : public MonteCarloNode
{

  private:
    vector<MonteCarloNode*> _noise_node_list;    //!< independent N(0,1) sources (the vector of raw noises)
    vector<MonteCarloNode*> _cholesky_node_list; //!< the matching row L_i of the Cholesky factor

  public:
    //! correlated increment = dot(noise, cholesky row): the i-th correlated factor
    void ComputeValue( size_t DateIndex ) override;
    //! each (noise, cholesky) pair is a child at the same date
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    vector<MonteCarloNode*> GetNoiseNodeList();    //!< the independent noise nodes (graph wiring)
    vector<MonteCarloNode*> GetCholeskyNodeList(); //!< the Cholesky-row coefficient nodes

    //! setter
    void PushNoiseNode( MonteCarloNode* N );    //!< append one independent noise (paired with PushCholeskyNode)
    void PushCholeskyNode( MonteCarloNode* N ); //!< append the matching Cholesky coefficient L_i

    CorrelatedNoiseNode( const string& Name );
    ~CorrelatedNoiseNode() override;
};
