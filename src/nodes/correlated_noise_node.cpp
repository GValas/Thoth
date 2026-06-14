#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

CorrelatedNoiseNode::CorrelatedNoiseNode( const string& Name ) : MonteCarloNode( Name )
{
}

CorrelatedNoiseNode::~CorrelatedNoiseNode() = default;

void CorrelatedNoiseNode::ComputeValue( size_t DateIndex )
{
    double x = 0;
    for ( size_t i = 0;
          i < _noise_node_list.size();
          i++ )
    {
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

vector<MonteCarloNode*> CorrelatedNoiseNode::GetNoiseNodeList()
{
    return _noise_node_list;
}

vector<MonteCarloNode*> CorrelatedNoiseNode::GetCholeskyNodeList()
{
    return _cholesky_node_list;
}

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
