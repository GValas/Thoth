#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

SpotDiffusionNode::SpotDiffusionNode( const string& Name ) : MonteCarloNode( Name )
{
    _local_vol_node = nullptr;
    _drift_node = nullptr;
    _brownian_node = nullptr;
    _spot = 0;
}

SpotDiffusionNode::~SpotDiffusionNode() = default;

void SpotDiffusionNode::ComputeValue( size_t DateIndex )
{
    //! diffusion value
    if ( DateIndex > 0 )
    {
        double s0 = _value_list[DateIndex - 1];
        double w1 = _brownian_node->GetValue( DateIndex );
        double w0 = _brownian_node->GetValue( DateIndex - 1 );
        double dw = w1 - w0;
        double r = _drift_node->GetValue( DateIndex );
        double v = _local_vol_node->GetValue( DateIndex );
        double dt = _dt_list[DateIndex];
        _value_list[DateIndex] = s0 * exp( ( r - v * v / 2 ) * dt + v * dw );
    }
    //! spot
    else if ( DateIndex == 0 )
    {
        _value_list[DateIndex] = _spot;
    }
    //! spot record
    else
    {
        _value_list[DateIndex] = 0;
    }
}

void SpotDiffusionNode::SetLocalVolNode( MonteCarloNode* N )
{
    _local_vol_node = N;
}

void SpotDiffusionNode::SetDriftNode( MonteCarloNode* N )
{
    _drift_node = N;
}

void SpotDiffusionNode::SetBrownianNode( MonteCarloNode* N )
{
    _brownian_node = N;
}

void SpotDiffusionNode::SetSpot( double Spot )
{
    _spot = Spot;
    _value_list[0] = _spot;
}

MonteCarloNode* SpotDiffusionNode::GetBrownianNode()
{
    return _brownian_node;
}

MonteCarloNode* SpotDiffusionNode::GetDriftNode()
{
    return _drift_node;
}

MonteCarloNode* SpotDiffusionNode::GetLocalVolNode()
{
    return _local_vol_node;
}

bool SpotDiffusionNode::IsConstant( size_t DateIndex )
{
    return ( DateIndex == 0 );
}

void SpotDiffusionNode::GetDateDependencies( size_t DateIndex,
                                             vector<MonteCarloNode*>& NodeList,
                                             vector<size_t>& DateList )
{
    if ( DateIndex > 0 )
    {
        NodeList.push_back( _local_vol_node );
        DateList.push_back( DateIndex );
        NodeList.push_back( _drift_node );
        DateList.push_back( DateIndex );
        NodeList.push_back( _brownian_node );
        DateList.push_back( DateIndex );
        NodeList.push_back( _brownian_node );
        DateList.push_back( DateIndex - 1 );
        NodeList.push_back( this );
        DateList.push_back( DateIndex - 1 );
    }
}
