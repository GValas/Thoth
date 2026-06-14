#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

ContractNode::ContractNode( const string& Name ) : MonteCarloNode( Name )
{
    _is_indicator = true; //!< per-contract premium/trust is reported
}

ContractNode::~ContractNode() = default;

void ContractNode::ComputeValue( size_t DateIndex )
{
    double x = 0;

    for ( size_t i = 0;
          i < _flow_node_list.size();
          i++ )
    {

        // disc factor
        double df = 1;
        if ( DateIndex != _flow_date_index_list[i] )
        {
            double dt1 = _t_list[DateIndex];
            double dt2 = _t_list[_flow_date_index_list[i]];
            double r1 = _rate_curve_node->GetValue( DateIndex );
            double r2 = _rate_curve_node->GetValue( _flow_date_index_list[i] );
            df = exp( dt1 * r1 - dt2 * r2 );
        }
        x += _flow_node_list[i]->GetValue( _flow_date_index_list[i] ) * df;
    }
    _value_list[DateIndex] = x;
}

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

vector<size_t> ContractNode::GetFlowDateIndexList()
{
    return _flow_date_index_list;
}

vector<MonteCarloNode*> ContractNode::GetFlowNodeList()
{
    return _flow_node_list;
}

MonteCarloNode* ContractNode::GetRateCurveNode()
{
    return _rate_curve_node;
}

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
