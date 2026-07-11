#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

HybridSpotNode::HybridSpotNode( const string& Name ) : MonteCarloNode( Name )
{
}

HybridSpotNode::~HybridSpotNode() = default;

//! hybrid spot = deterministic-carry spot * exp( X(t) )
void HybridSpotNode::ComputeValue( size_t DateIndex )
{
    _value_list[DateIndex] =
        _spot_node->GetValue( DateIndex ) * exp( _exponent_node->GetValue( DateIndex ) );
}

void HybridSpotNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}

void HybridSpotNode::SetExponentNode( MonteCarloNode* N )
{
    _exponent_node = N;
}

//! both inputs at the same date
void HybridSpotNode::GetDateDependencies( size_t DateIndex,
                                          vector<MonteCarloNode*>& NodeList,
                                          vector<size_t>& DateList )
{
    NodeList.push_back( _spot_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _exponent_node );
    DateList.push_back( DateIndex );
}
