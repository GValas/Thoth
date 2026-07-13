#include "thoth.hpp"
#include "finance.hpp"
#include "nodes.hpp"

//************************************************************************/

//! Construct with the spot unwired; the builder attaches it before pricing.
DigitalFlowNode::DigitalFlowNode( const string& Name ) : MonteCarloNode( Name )
{
    _spot_node = nullptr;
}

DigitalFlowNode::~DigitalFlowNode() = default;

//! Emit the digital cash flow. Side effect: writes _value_list[DateIndex]. At the flow
//! date it reads S_T and returns the binary payoff (payoff_digital_option, shared with the
//! contract's Intrinsic); every other date pays nothing.
void DigitalFlowNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex == _flow_date_index )
    {
        double s = _spot_node->GetValue( _flow_date_index );
        _value_list[DateIndex] = payoff_digital_option( s, _strike, _type, _is_cash, _cash_amount );
    }
    else
    {
        _value_list[DateIndex] = 0;
    }
}

//! set the strike K.
void DigitalFlowNode::SetStrike( double Strike )
{
    _strike = Strike;
}

//! set the option type (call / put).
void DigitalFlowNode::SetType( OptionType Type )
{
    _type = Type;
}

//! cash-or-nothing (true) vs asset-or-nothing (false).
void DigitalFlowNode::SetCashOrNothing( bool IsCash )
{
    _is_cash = IsCash;
}

//! set the fixed cash payout Q (cash-or-nothing only).
void DigitalFlowNode::SetCashAmount( double Amount )
{
    _cash_amount = Amount;
}

//! set the maturity (flow) date index at which the option pays.
void DigitalFlowNode::SetFlowDateIndex( size_t DateIndex )
{
    _flow_date_index = DateIndex;
}

//! wire the underlying spot node.
void DigitalFlowNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}

//! Depend on the spot only at the flow date — there is no cash flow on other dates, so no
//! dependency is declared there.
void DigitalFlowNode::GetDateDependencies( size_t DateIndex,
                                           vector<MonteCarloNode*>& NodeList,
                                           vector<size_t>& DateList )
{
    if ( _flow_date_index == DateIndex )
    {
        NodeList.push_back( _spot_node );
        DateList.push_back( DateIndex );
    }
}
