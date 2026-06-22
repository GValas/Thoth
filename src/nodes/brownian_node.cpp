#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

BrownianNode::BrownianNode( const string& Name ) : MonteCarloNode( Name ) {}

BrownianNode::~BrownianNode() = default;

//! exact Wiener increment recursion: W is a martingale with independent N(0,dt_i)
//! increments, so W_i = W_{i-1} + sqrt(dt_i) * Z_i is exact (no discretisation bias)
void BrownianNode::ComputeValue( size_t DateIndex )
{
    // w(0) = 0
    if ( DateIndex == 0 )
    {
        _value_list[DateIndex] = 0;
    }
    //! wiener discretization : add sqrt(dt_i)*Z_i to the running path (dt precomputed in the base)
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

//! W(0)=0 is path-independent, so the scheduler treats date 0 as a constant
bool BrownianNode::IsConstant( size_t DateIndex )
{
    return ( DateIndex == 0 );
}

//! recursion makes each step depend on the current noise and on this node's own
//! previous value (self-edge at DateIndex-1); date 0 has no dependencies
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
        //! self-dependency on the accumulated path so far
        NodeList.push_back( this );
        DateList.push_back( DateIndex - 1 );
    }
}
