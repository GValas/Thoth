#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

VanillaFlowNode::VanillaFlowNode( const string& Name ) : MonteCarloNode( Name )
{
    _spot_node = nullptr;
}

VanillaFlowNode::~VanillaFlowNode() = default;

void VanillaFlowNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex == _flow_date_index )
    {
        double s = _spot_node->GetValue( _flow_date_index );
        _value_list[DateIndex] = payoff_vanilla( s, _strike, _type, false, 0, true, _floor );
    }
    else
    {
        _value_list[DateIndex] = 0;
    }
}

void VanillaFlowNode::SetStrike( double Strike )
{
    _strike = Strike;
}

void VanillaFlowNode::SetFloor( double Floor )
{
    _floor = Floor;
}

void VanillaFlowNode::SetType( OptionType Type )
{
    _type = Type;
}

void VanillaFlowNode::SetFlowDateIndex( size_t DateIndex )
{
    _flow_date_index = DateIndex;
}

void VanillaFlowNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}

size_t VanillaFlowNode::GetFlowDateIndex()
{
    return _flow_date_index;
}

MonteCarloNode* VanillaFlowNode::GetSpotNode()
{
    return _spot_node;
}

void VanillaFlowNode::GetDateDependencies( size_t DateIndex,
                                           vector<MonteCarloNode*>& NodeList,
                                           vector<size_t>& DateList )
{
    if ( _flow_date_index == DateIndex )
    {
        NodeList.push_back( _spot_node );
        DateList.push_back( DateIndex );
    }
}
