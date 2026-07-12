#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

AsianFlowNode::AsianFlowNode( const string& Name ) : MonteCarloNode( Name )
{
}

AsianFlowNode::~AsianFlowNode() = default;

//! average the spot over the schedule and emit the average-price payoff at maturity
void AsianFlowNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex != _flow_date_index )
    {
        _value_list[DateIndex] = 0;
        return;
    }

    double sum = 0;
    for ( size_t j : _observation_date_index )
    {
        sum += _spot_node->GetValue( j );
    }
    const double average = _observation_date_index.empty()
                               ? 0
                               : sum / (double)_observation_date_index.size();

    const double omega = ( _type == OptionType::Call ) ? 1.0 : -1.0;
    _value_list[DateIndex] = _notional * std::max( omega * ( average - _strike ), 0.0 );
}

void AsianFlowNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}

void AsianFlowNode::SetStrike( double Strike )
{
    _strike = Strike;
}

void AsianFlowNode::SetType( OptionType Type )
{
    _type = Type;
}

void AsianFlowNode::SetNotional( double Notional )
{
    _notional = Notional;
}

void AsianFlowNode::SetFlowDateIndex( size_t DateIndex )
{
    _flow_date_index = DateIndex;
}

void AsianFlowNode::SetObservationDateIndices( const vector<size_t>& DateIndices )
{
    _observation_date_index = DateIndices;
}

//! at the flow date, declare the spot at every averaging observation
void AsianFlowNode::GetDateDependencies( size_t DateIndex,
                                         vector<MonteCarloNode*>& NodeList,
                                         vector<size_t>& DateList )
{
    if ( DateIndex != _flow_date_index )
    {
        return;
    }
    for ( size_t j : _observation_date_index )
    {
        NodeList.push_back( _spot_node );
        DateList.push_back( j );
    }
}
