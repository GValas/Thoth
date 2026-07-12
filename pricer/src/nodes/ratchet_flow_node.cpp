#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

RatchetFlowNode::RatchetFlowNode( const string& Name ) : MonteCarloNode( Name )
{
}

RatchetFlowNode::~RatchetFlowNode() = default;

//! sum the clipped period returns over the boundary schedule and emit the coupon
void RatchetFlowNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex != _flow_date_index )
    {
        _value_list[DateIndex] = 0;
        return;
    }

    double coupon = 0;
    double prev = _observation_date_index.empty()
                      ? 0
                      : _spot_node->GetValue( _observation_date_index.front() );
    for ( size_t i = 1; i < _observation_date_index.size(); i++ )
    {
        const double s = _spot_node->GetValue( _observation_date_index[i] );
        const double r = s / prev - 1.0;
        //! lock in the clipped period return (the "ratchet": a capped gain cannot
        //! be given back by a later fall)
        coupon += std::max( _local_floor, std::min( _local_cap, r ) );
        prev = s;
    }

    //! global clip: the floor (default 0) is the note's capital protection
    coupon = std::max( _global_floor, coupon );
    if ( _has_global_cap )
    {
        coupon = std::min( _global_cap, coupon );
    }
    _value_list[DateIndex] = _notional * coupon;
}

void RatchetFlowNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}

void RatchetFlowNode::SetNotional( double Notional )
{
    _notional = Notional;
}

void RatchetFlowNode::SetFlowDateIndex( size_t DateIndex )
{
    _flow_date_index = DateIndex;
}

void RatchetFlowNode::SetObservationDateIndices( const vector<size_t>& DateIndices )
{
    _observation_date_index = DateIndices;
}

void RatchetFlowNode::SetLocalClip( double Floor, double Cap )
{
    _local_floor = Floor;
    _local_cap = Cap;
}

void RatchetFlowNode::SetGlobalClip( double Floor, bool HasCap, double Cap )
{
    _global_floor = Floor;
    _has_global_cap = HasCap;
    _global_cap = Cap;
}

//! at the flow date, declare the spot at every period boundary
void RatchetFlowNode::GetDateDependencies( size_t DateIndex,
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
