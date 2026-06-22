#include "thoth.hpp"
#include "nodes.hpp"

RainbowNode::RainbowNode( const string& Name ) : MonteCarloNode( Name )
{
}

RainbowNode::~RainbowNode() = default;

//! Reduce the members to the best/worst rebased performance for this date. Side
//! effect: writes _value_list[DateIndex]. Seeds the accumulator with the identity
//! for the chosen reduction (-inf for max, +inf for min) so the first member
//! always replaces it.
void RainbowNode::ComputeValue( size_t DateIndex )
{
    //! max (best-of) or min (worst-of) over the rebased member performances
    double x = _best ? -std::numeric_limits<double>::infinity()
                     : std::numeric_limits<double>::infinity();
    for ( size_t i = 0; i < _underlying_node_list.size(); i++ )
    {
        //! rebase to performance (100 * S_i/S_i0) so members on different absolute
        //! levels are compared fairly, then fold into the running best/worst.
        double perf = _underlying_node_list[i]->GetValue( DateIndex ) * _weight_list[i];
        x = _best ? std::max( x, perf ) : std::min( x, perf );
    }
    _value_list[DateIndex] = x;
}

//! Declare every member spot node at this date (all members are read per date).
void RainbowNode::GetDateDependencies( size_t DateIndex,
                                       vector<MonteCarloNode*>& NodeList,
                                       vector<size_t>& DateList )
{
    for ( auto* n : _underlying_node_list )
    {
        NodeList.push_back( n );
        DateList.push_back( DateIndex );
    }
}

//! append a member spot node (paired with a PushWeight call).
void RainbowNode::PushUnderlying( MonteCarloNode* N )
{
    _underlying_node_list.push_back( N );
}

//! append a member's 100/S_i0 rebasing factor, aligned with the spot-node list.
void RainbowNode::PushWeight( double Weight )
{
    _weight_list.push_back( Weight );
}

//! choose the reduction: true = best-of (max), false = worst-of (min).
void RainbowNode::SetBest( bool Best )
{
    _best = Best;
}
