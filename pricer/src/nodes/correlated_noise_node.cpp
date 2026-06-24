#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

CorrelatedNoiseNode::CorrelatedNoiseNode( const string& Name ) : MonteCarloNode( Name )
{
}

CorrelatedNoiseNode::~CorrelatedNoiseNode() = default;

//! one row of L*Z: a correlated Gaussian with the desired covariance is built as
//! L*Z from independent Z, where L is the lower-triangular Cholesky factor of the
//! correlation matrix. This node holds one output row, so it sums noise_i * L_i.
void CorrelatedNoiseNode::ComputeValue( size_t DateIndex )
{
    double x = 0;
    for ( size_t i = 0;
          i < _noise_node_list.size();
          i++ )
    {
        //! Cholesky coefficients are constant nodes; the noise is the only path-varying part
        x += _noise_node_list[i]->GetValue( DateIndex ) *
             _cholesky_node_list[i]->GetValue( DateIndex );
    }
    _value_list[DateIndex] = x;
}

void CorrelatedNoiseNode::PushNoiseNode( MonteCarloNode* N )
{
    _noise_node_list.push_back( N );
}

void CorrelatedNoiseNode::PushCholeskyNode( MonteCarloNode* N )
{
    _cholesky_node_list.push_back( N );
}
//! each term of the dot product needs its noise and its Cholesky coefficient at the
//! same date
void CorrelatedNoiseNode::GetDateDependencies( size_t DateIndex,
                                               vector<MonteCarloNode*>& NodeList,
                                               vector<size_t>& DateList )
{
    for ( size_t i = 0;
          i < _noise_node_list.size();
          i++ )
    {
        NodeList.push_back( _noise_node_list[i] );
        DateList.push_back( DateIndex );
        NodeList.push_back( _cholesky_node_list[i] );
        DateList.push_back( DateIndex );
    }
}
