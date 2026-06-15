#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

QuantoAdjustmentNode::QuantoAdjustmentNode( const string& Name ) : MonteCarloNode( Name )
{
    _udl_fx_correl_node = nullptr;
    _udl_vol_node = nullptr;
    _fx_vol_node = nullptr;
    _udl_spot_node = nullptr;
}

QuantoAdjustmentNode::~QuantoAdjustmentNode() = default;

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

    double c = _udl_fx_correl_node->GetValue( DateIndex );
    double v = _udl_vol_node->GetValue( DateIndex );
    double w = _fx_vol_node->GetValue( DateIndex );
    double dt = _t_list[DateIndex];
    double q = exp( -v * w * c * dt );
    _value_list[DateIndex] = s * q;
}

void QuantoAdjustmentNode::SetUdlVolNode( MonteCarloNode* N )
{
    _udl_vol_node = N;
}

void QuantoAdjustmentNode::SetFxVolNode( MonteCarloNode* N )
{
    _fx_vol_node = N;
}

void QuantoAdjustmentNode::SetUdlFxCorrelNode( MonteCarloNode* N )
{
    _udl_fx_correl_node = N;
}

void QuantoAdjustmentNode::SetUdlSpotNode( MonteCarloNode* N )
{
    _udl_spot_node = N;
}

MonteCarloNode* QuantoAdjustmentNode::GetUdlVolNode()
{
    return _udl_vol_node;
}

MonteCarloNode* QuantoAdjustmentNode::GetFxVolNode()
{
    return _fx_vol_node;
}

MonteCarloNode* QuantoAdjustmentNode::GetUdlFxCorrelNode()
{
    return _udl_fx_correl_node;
}

MonteCarloNode* QuantoAdjustmentNode::GetUdlSpotNode()
{
    return _udl_spot_node;
}

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
