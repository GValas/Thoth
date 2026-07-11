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

    //! Annualized realized variance = sum of squared log-returns / T. Continuous
    //! observation (empty schedule) sums EVERY diffusion step — the discretised
    //! quadratic variation, matching the continuous ANA replication and the PDE
    //! accumulated-variance fair value. A discrete schedule sums only over its
    //! fixing dates (today is the implicit first fixing), which adds the
    //! deterministic drift^2 term the ANA/PDE correct for via
    //! VarianceSwap::ObservationDriftVariance.
    //! Seed prev with S_0, then roll forward so each interval uses ln(S_j / S_i).
    double sum2 = 0;
    double prev = _spot_node->GetValue( 0 );
    if ( _observation_date_index.empty() )
    {
        for ( size_t j = 1; j <= _flow_date_index; j++ )
        {
            double s = _spot_node->GetValue( j );
            double r = log( s / prev );
            sum2 += r * r;
            prev = s;
        }
    }
    else
    {
        for ( size_t j : _observation_date_index )
        {
            double s = _spot_node->GetValue( j );
            double r = log( s / prev );
            sum2 += r * r;
            prev = s;
        }
    }
    //! annualizer: the whole observation window for a seasoned swap (start ->
    //! maturity, with the realised past sum added), the grid's maturity time
    //! otherwise — the historic spot-started behaviour, bit for bit
    double T = ( _total_year_fraction > 0 ) ? _total_year_fraction
                                            : _t_list[_flow_date_index];
    double realized_variance = ( T > 0 ) ? ( _past_variance + sum2 ) / T : 0;

    _value_list[DateIndex] = _notional * ( realized_variance - _strike_variance );
}

void VarianceSwapFlowNode::SetPastVariance( double PastSum2 )
{
    _past_variance = PastSum2;
}

void VarianceSwapFlowNode::SetTotalYearFraction( double TotalYearFraction )
{
    _total_year_fraction = TotalYearFraction;
}

//! At maturity, declare the spot at every date 0..maturity (the variance estimator
//! reads the entire path). Off the flow date there is nothing to declare.
void VarianceSwapFlowNode::GetDateDependencies( size_t DateIndex,
                                                vector<MonteCarloNode*>& NodeList,
                                                vector<size_t>& DateList )
{
    //! the maturity flow needs the spot at every observation date up to maturity
    //! (the whole grid when continuous, only the fixing schedule when discrete)
    if ( DateIndex != _flow_date_index )
    {
        return;
    }
    if ( _observation_date_index.empty() )
    {
        for ( size_t j = 0; j <= _flow_date_index; j++ )
        {
            NodeList.push_back( _spot_node );
            DateList.push_back( j );
        }
    }
    else
    {
        NodeList.push_back( _spot_node ); //!< today (index 0): the first fixing
        DateList.push_back( 0 );
        for ( size_t j : _observation_date_index )
        {
            NodeList.push_back( _spot_node );
            DateList.push_back( j );
        }
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

//! set the discrete fixing schedule (sorted date indices; empty = continuous).
void VarianceSwapFlowNode::SetObservationDateIndices( const vector<size_t>& DateIndices )
{
    _observation_date_index = DateIndices;
}
