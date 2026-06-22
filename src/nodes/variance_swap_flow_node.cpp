#include "thoth.hpp"
#include "nodes.hpp"

VarianceSwapFlowNode::VarianceSwapFlowNode( const string& Name ) : MonteCarloNode( Name )
{
}

VarianceSwapFlowNode::~VarianceSwapFlowNode() = default;

//! Emit the variance-swap cash flow. Side effect: writes _value_list[DateIndex].
//! At maturity it walks the whole spot path to form annualized realized variance,
//! then returns notional * (RV - K_var); every other date pays 0.
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
    //! accumulate the sum of squared log-returns (quadratic variation). Seed prev
    //! with S_0, then roll forward so each step uses ln(S_j / S_{j-1}).
    double sum2 = 0;
    double prev = _spot_node->GetValue( 0 );
    for ( size_t j = 1; j <= _flow_date_index; j++ )
    {
        double s = _spot_node->GetValue( j );
        double r = log( s / prev );
        sum2 += r * r;
        prev = s;
    }
    double T = _t_list[_flow_date_index]; //!< year-fraction to maturity (annualizer)
    double realized_variance = ( T > 0 ) ? sum2 / T : 0;

    _value_list[DateIndex] = _notional * ( realized_variance - _strike_variance );
}

//! At maturity, declare the spot at every date 0..maturity (the variance estimator
//! reads the entire path). Off the flow date there is nothing to declare.
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

//! wire the underlying spot node.
void VarianceSwapFlowNode::SetSpotNode( MonteCarloNode* N )
{
    _spot_node = N;
}

//! set the strike variance K_var (decimal^2).
void VarianceSwapFlowNode::SetStrikeVariance( double StrikeVariance )
{
    _strike_variance = StrikeVariance;
}

//! set the variance notional (cash per unit of variance).
void VarianceSwapFlowNode::SetNotional( double Notional )
{
    _notional = Notional;
}

//! set the maturity (flow) date index.
void VarianceSwapFlowNode::SetFlowDateIndex( size_t DateIndex )
{
    _flow_date_index = DateIndex;
}
