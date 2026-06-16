#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

LocalVolatilityNode::LocalVolatilityNode( const string& Name ) : MonteCarloNode( Name )
{

    _spot_node = nullptr;
}

LocalVolatilityNode::~LocalVolatilityNode() = default;

void LocalVolatilityNode::ComputeValue( size_t DateIndex )
{
    la_vector* v = _vol_vector_list[DateIndex];
    double ln_step = _ln_step_list[DateIndex];
    size_t offset = _offset_list[DateIndex];
    double s = _spot_node->GetValue( DateIndex - 1 );
    size_t i = (size_t)( 0.5 + log( s ) / ln_step ) - offset + 1;
    _value_list[DateIndex] = la_vector_get( v, i );
}

void LocalVolatilityNode::SetSpotNode( MonteCarloNode* SpotNode )
{
    _spot_node = SpotNode;
}

void LocalVolatilityNode::PushLnStep( double LnStep )
{
    _ln_step_list.push_back( LnStep );
}

void LocalVolatilityNode::PushOffset( size_t Offset )
{
    _offset_list.push_back( Offset );
}

void LocalVolatilityNode::PushVolVector( la_vector* VolVector )
{
    _vol_vector_list.push_back( VolVector );
}

void LocalVolatilityNode::GetDateDependencies( size_t /*DateIndex*/,
                                               vector<MonteCarloNode*>& /*NodeList*/,
                                               vector<size_t>& /*DateList*/ )
{
}
