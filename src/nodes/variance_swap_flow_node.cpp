#include "thoth.hpp"
#include "nodes.hpp"

VarianceSwapFlowNode::VarianceSwapFlowNode( const string& Name ) : MonteCarloNode( Name )
{
}

VarianceSwapFlowNode::~VarianceSwapFlowNode() = default;

void VarianceSwapFlowNode::ComputeValue( size_t DateIndex )
{
    //! the flow only pays at maturity
    if ( DateIndex != _flow_date_index )
    {
        _value_list[DateIndex] = 0;
        return;
    }

    //! Annualized realized variance = the discretised quadratic variation over the
    //! whole simulated path: squared log-returns at EVERY diffusion step, / T. This
    //! is a continuously-observed variance swap (no intermediate fixing schedule —
    //! GetFixingDates() is just the maturity), so summing all steps is the correct
    //! estimator and it converges to / agrees with the continuous ANA replication
    //! and PDE accumulated-variance fair value. A discretely-observed swap would
    //! instead sum only over its fixing dates.
    double sum2 = 0;
    double prev = _spot_node->GetValue( 0 );
    for ( size_t j = 1; j <= _flow_date_index; j++ )
    {
        double s = _spot_node->GetValue( j );
        double r = log( s / prev );
        sum2 += r * r;
        prev = s;
    }
    double T = _t_list[_flow_date_index];
    double realized_variance = ( T > 0 ) ? sum2 / T : 0;

    _value_list[DateIndex] = _notional * ( realized_variance - _strike_variance );
}

void VarianceSwapFlowNode::GetDateDependencies( size_t DateIndex,
                                                vector<MonteCarloNode*>& NodeList,
                                                vector<size_t>& DateList )
{
    //! the maturity flow needs the spot at every observation date up to maturity
    if ( DateIndex != _flow_date_index )
    {
        return;
    }
    for ( size_t j = 0; j <= _flow_date_index; j++ )
    {
        NodeList.push_back( _spot_node );
        DateList.push_back( j );
    }
}

void VarianceSwapFlowNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}

void VarianceSwapFlowNode::SetStrikeVariance( double StrikeVariance )
{
    _strike_variance = StrikeVariance;
}

void VarianceSwapFlowNode::SetNotional( double Notional )
{
    _notional = Notional;
}

void VarianceSwapFlowNode::SetFlowDateIndex( size_t DateIndex )
{
    _flow_date_index = DateIndex;
}
