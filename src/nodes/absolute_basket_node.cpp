#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

//! the basket carries no own state; weights/components are pushed after construction
AbsoluteBasketNode::AbsoluteBasketNode( const string& Name ) : MonteCarloNode( Name )
{
}

AbsoluteBasketNode::~AbsoluteBasketNode() = default;

//! basket level = sum_i w_i * S_i at this date (absolute / unrebased: the raw
//! weighted sum of spots, not normalised to 100 or to t0 levels)
void AbsoluteBasketNode::ComputeValue( size_t DateIndex )
{
    double x = 0;
    for ( size_t i = 0;
          i < _underlying_node_list.size();
          i++ )
    {
        //! _weight_list[i] is paired with _underlying_node_list[i] by push order
        x += _underlying_node_list[i]->GetValue( DateIndex ) * _weight_list[i];
    }
    _value_list[DateIndex] = x;
}

//! append a component spot node; must be matched by a PushWeight so the two
//! parallel vectors stay index-aligned
void AbsoluteBasketNode::PushUnderlying( MonteCarloNode* N )
{
    _underlying_node_list.push_back( N );
}

//! append the weight for the most recently pushed component
void AbsoluteBasketNode::PushWeight( double Weight )
{
    _weight_list.push_back( Weight );
}

//! the basket value at DateIndex needs every component spot at the *same* date,
//! so each is declared as a child at DateIndex for the topological sort
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
