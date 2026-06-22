#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

ContractNode::ContractNode( const string& Name ) : MonteCarloNode( Name )
{
    _is_indicator = true; //!< per-contract premium/trust is reported
}

ContractNode::~ContractNode() = default;

//! contract premium as seen at DateIndex: discount every flow from its flow date
//! back to DateIndex and sum. As an indicator node this per-contract premium is what
//! the reporting layer reads (mean + trust).
void ContractNode::ComputeValue( size_t DateIndex )
{
    double x = 0;

    for ( size_t i = 0;
          i < _flow_node_list.size();
          i++ )
    {

        // disc factor
        double df = 1;
        //! a flow paid exactly on DateIndex is undiscounted (df=1); otherwise discount it
        if ( DateIndex != _flow_date_index_list[i] )
        {
            //! continuously-compounded zero rates r(t) are stored cumulative, so the
            //! discount factor from the flow date t2 to the valuation date t1 is
            //! exp( r1*t1 - r2*t2 ) = exp(-r2*t2) / exp(-r1*t1) — the ratio of the
            //! two zero-coupon prices (works for both forward and backward t1 vs t2)
            double dt1 = _t_list[DateIndex];
            double dt2 = _t_list[_flow_date_index_list[i]];
            double r1 = _rate_curve_node->GetValue( DateIndex );
            double r2 = _rate_curve_node->GetValue( _flow_date_index_list[i] );
            df = exp( dt1 * r1 - dt2 * r2 );
        }
        //! the flow's value is always taken at its own flow date, then discounted
        x += _flow_node_list[i]->GetValue( _flow_date_index_list[i] ) * df;
    }
    _value_list[DateIndex] = x;
}

//! register a flow and its pay date together, keeping the two parallel vectors aligned
void ContractNode::PushFlowNode( MonteCarloNode* N,
                                 size_t FlowDateIndex )
{
    _flow_node_list.push_back( N );
    _flow_date_index_list.push_back( FlowDateIndex );
}

void ContractNode::SetRateCurveNode( MonteCarloNode* N )
{
    _rate_curve_node = N;
}
//! independent of the valuation DateIndex: each flow is needed at its own pay date,
//! together with the rate curve at that date for discounting
void ContractNode::GetDateDependencies( size_t /*DateIndex*/,
                                        vector<MonteCarloNode*>& NodeList,
                                        vector<size_t>& DateList )
{
    for ( size_t i = 0;
          i < _flow_date_index_list.size();
          i++ )
    {
        NodeList.push_back( _flow_node_list[i] );
        DateList.push_back( _flow_date_index_list[i] );
        NodeList.push_back( _rate_curve_node );
        DateList.push_back( _flow_date_index_list[i] );
    }
}
