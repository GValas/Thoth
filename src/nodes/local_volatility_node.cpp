#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

LocalVolatilityNode::LocalVolatilityNode( const string& Name ) : MonteCarloNode( Name )
{

    _spot_node = nullptr;
}

//! the per-date vol vectors are owned here (allocated by the node builder)
LocalVolatilityNode::~LocalVolatilityNode()
{
    for ( la_vector* v : _vol_vector_list )
    {
        la_vector_free( v );
    }
}

//! local vol at the spot reached on the previous step: read it off the precomputed
//! log-spot grid for this date by linear interpolation, clamped to the grid ends
//! (an extreme path reuses the boundary local vol rather than reading out of range).
void LocalVolatilityNode::ComputeValue( size_t DateIndex )
{
    const la_vector* v = _vol_vector_list[DateIndex];
    const double ln_step = _ln_step_list[DateIndex];
    const long offset = _offset_list[DateIndex];

    //! index 0 (today) never feeds a diffusion step; guard the DateIndex-1 read
    const size_t prev = ( DateIndex > 0 ) ? DateIndex - 1 : 0;
    const double s = _spot_node->GetValue( prev );

    const double f = log( s ) / ln_step - (double)offset; //!< fractional grid index
    const size_t last = v->size - 1;
    if ( f <= 0.0 )
    {
        _value_list[DateIndex] = la_vector_get( v, 0 );
    }
    else if ( f >= (double)last )
    {
        _value_list[DateIndex] = la_vector_get( v, last );
    }
    else
    {
        const size_t i = (size_t)f;
        const double w = f - (double)i;
        _value_list[DateIndex] = ( 1.0 - w ) * la_vector_get( v, i ) + w * la_vector_get( v, i + 1 );
    }
}

void LocalVolatilityNode::SetSpotNode( MonteCarloNode* SpotNode )
{
    _spot_node = SpotNode;
}

void LocalVolatilityNode::PushLnStep( double LnStep )
{
    _ln_step_list.push_back( LnStep );
}

void LocalVolatilityNode::PushOffset( long Offset )
{
    _offset_list.push_back( Offset );
}

void LocalVolatilityNode::PushVolVector( la_vector* VolVector )
{
    _vol_vector_list.push_back( VolVector );
}

//! the local vol at date k depends on the spot reached at the previous date, so
//! the scheduler must evaluate the spot path up to DateIndex-1 first
void LocalVolatilityNode::GetDateDependencies( size_t DateIndex,
                                               vector<MonteCarloNode*>& NodeList,
                                               vector<size_t>& DateList )
{
    if ( DateIndex > 0 && _spot_node )
    {
        NodeList.push_back( _spot_node );
        DateList.push_back( DateIndex - 1 );
    }
}
