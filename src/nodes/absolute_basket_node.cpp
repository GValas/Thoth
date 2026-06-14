#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

AbsoluteBasketNode::AbsoluteBasketNode( const string& Name ) : MonteCarloNode( Name )
{
}

AbsoluteBasketNode::~AbsoluteBasketNode() = default;

void AbsoluteBasketNode::ComputeValue( size_t DateIndex )
{
    double x = 0;
    for ( size_t i = 0;
          i < _underlying_node_list.size();
          i++ )
    {
        x += _underlying_node_list[i]->GetValue( DateIndex ) * _weight_list[i];
    }
    _value_list[DateIndex] = x;
}

void AbsoluteBasketNode::PushUnderlying( MonteCarloNode* N )
{
    _underlying_node_list.push_back( N );
}

void AbsoluteBasketNode::PushWeight( double Weight )
{
    _weight_list.push_back( Weight );
}

void AbsoluteBasketNode::GetDateDependencies( size_t DateIndex,
                                              vector<MonteCarloNode*>& NodeList,
                                              vector<size_t>& DateList )
{
    for ( auto i : _underlying_node_list )
    {
        NodeList.push_back( i );
        DateList.push_back( DateIndex );
    }
}

vector<MonteCarloNode*> AbsoluteBasketNode::GetUnderlyingNodeList()
{
    return _underlying_node_list;
}
