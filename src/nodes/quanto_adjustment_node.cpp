#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

//! Construct with all four inputs unwired; the graph builder sets them before use.
QuantoAdjustmentNode::QuantoAdjustmentNode( const string& Name ) : MonteCarloNode( Name )
{
    _udl_fx_correl_node = nullptr;
    _udl_vol_node = nullptr;
    _fx_vol_node = nullptr;
    _udl_spot_node = nullptr;
}

QuantoAdjustmentNode::~QuantoAdjustmentNode() = default;

//! Compute the quanto-adjusted spot for this date. Side effect: writes
//! _value_list[DateIndex]. Reads the spot and, away from t=0, the vols and
//! correlation to form the multiplier q = exp(-sigma_S sigma_FX rho t).
void QuantoAdjustmentNode::ComputeValue( size_t DateIndex )
{
    double s = _udl_spot_node->GetValue( DateIndex );

    //! today (dt = 0): the quanto drift adjustment over a zero horizon is 1, so
    //! the value is just the spot. Leaving index 0 unset (the old behaviour) left
    //! it at 0, which is invisible to European pricing — only the maturity flow is
    //! read — but corrupts the recorded American (LSM) path at t = 0.
    if ( DateIndex == 0 )
    {
        _value_list[DateIndex] = s;
        return;
    }

    double c = _udl_fx_correl_node->GetValue( DateIndex ); //!< rho(S, FX)
    double v = _udl_vol_node->GetValue( DateIndex );       //!< sigma_S
    double w = _fx_vol_node->GetValue( DateIndex );        //!< sigma_FX
    double dt = _t_list[DateIndex];                        //!< horizon t (year-fraction from t0)
    //! deterministic quanto multiplier: the change-of-measure drift -rho sigma_S
    //! sigma_FX accumulated over [0, t] becomes a scale factor on the spot.
    double q = exp( -v * w * c * dt );
    _value_list[DateIndex] = s * q;
}

//! wire sigma_S.
void QuantoAdjustmentNode::SetUdlVolNode( MonteCarloNode* N )
{
    _udl_vol_node = N;
}

//! wire sigma_FX.
void QuantoAdjustmentNode::SetFxVolNode( MonteCarloNode* N )
{
    _fx_vol_node = N;
}

//! wire rho(S, FX).
void QuantoAdjustmentNode::SetUdlFxCorrelNode( MonteCarloNode* N )
{
    _udl_fx_correl_node = N;
}

//! wire the diffused spot to adjust.
void QuantoAdjustmentNode::SetUdlSpotNode( MonteCarloNode* N )
{
    _udl_spot_node = N;
}
//! Declare the four inputs (vols, correlation, spot), all at this date index.
void QuantoAdjustmentNode::GetDateDependencies( size_t DateIndex,
                                                vector<MonteCarloNode*>& NodeList,
                                                vector<size_t>& DateList )
{
    NodeList.push_back( _udl_vol_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _fx_vol_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _udl_fx_correl_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _udl_spot_node );
    DateList.push_back( DateIndex );
}
