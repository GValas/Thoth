#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

//! Construct with the spot unwired; the builder attaches it before pricing.
VanillaFlowNode::VanillaFlowNode( const string& Name ) : MonteCarloNode( Name )
{
    _spot_node = nullptr;
}

VanillaFlowNode::~VanillaFlowNode() = default;

//! Emit the option cash flow. Side effect: writes _value_list[DateIndex]. At the
//! flow date it reads S_T and returns the floored vanilla intrinsic; every other
//! date pays nothing.
void VanillaFlowNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex == _flow_date_index )
    {
        double s = _spot_node->GetValue( _flow_date_index );
        //! floored vanilla intrinsic max(phi(S-K), floor); the false/0/true args
        //! select the plain (non-digital) payoff with the floor applied.
        _value_list[DateIndex] = payoff_vanilla( s, _strike, _type, false, 0, true, _floor );
    }
    else
    {
        _value_list[DateIndex] = 0;
    }
}

//! set the strike K.
void VanillaFlowNode::SetStrike( double Strike )
{
    _strike = Strike;
}

//! set the lower floor on the payoff.
void VanillaFlowNode::SetFloor( double Floor )
{
    _floor = Floor;
}

//! set the option type (call / put).
void VanillaFlowNode::SetType( OptionType Type )
{
    _type = Type;
}

//! set the maturity (flow) date index at which the option pays.
void VanillaFlowNode::SetFlowDateIndex( size_t DateIndex )
{
    _flow_date_index = DateIndex;
}

//! wire the underlying spot node.
void VanillaFlowNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}
//! Depend on the spot only at the flow date — there is no cash flow on other dates,
//! so no dependency is declared there.
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
