#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

BrownianNode::BrownianNode( const string& Name ) : MonteCarloNode( Name ) {}

BrownianNode::~BrownianNode() = default;

void BrownianNode::ComputeValue( size_t DateIndex )
{
    // w(0) = 0
    if ( DateIndex == 0 )
    {
        _value_list[DateIndex] = 0;
    }
    //! wiener discretization
    else
    {
        _value_list[DateIndex] = _value_list[DateIndex - 1] + _sqrt_dt_list[DateIndex] * _noise_node->GetValue( DateIndex );
    }
}

void BrownianNode::SetNoiseNode( MonteCarloNode* NoiseNode )
{
    _noise_node = NoiseNode;
}

MonteCarloNode* BrownianNode::GetNoiseNode()
{
    return _noise_node;
}

bool BrownianNode::IsConstant( size_t DateIndex )
{
    return ( DateIndex == 0 );
}

void BrownianNode::GetDateDependencies( size_t DateIndex,
                                        vector<MonteCarloNode*>& NodeList,
                                        vector<size_t>& DateList )
{
    if ( DateIndex > 0 )
    {
        if ( _noise_node )
        {
            NodeList.push_back( _noise_node );
            DateList.push_back( DateIndex );
        }
        NodeList.push_back( this );
        DateList.push_back( DateIndex - 1 );
    }
}
