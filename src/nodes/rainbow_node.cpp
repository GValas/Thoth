#include "thoth.hpp"
#include "nodes.hpp"

RainbowNode::RainbowNode( const string& Name ) : MonteCarloNode( Name )
{
}

RainbowNode::~RainbowNode() = default;

void RainbowNode::ComputeValue( size_t DateIndex )
{
    //! max (best-of) or min (worst-of) over the rebased member performances
    double x = _best ? -std::numeric_limits<double>::infinity()
                     : std::numeric_limits<double>::infinity();
    for ( size_t i = 0; i < _underlying_node_list.size(); i++ )
    {
        double perf = _underlying_node_list[i]->GetValue( DateIndex ) * _weight_list[i];
        x = _best ? std::max( x, perf ) : std::min( x, perf );
    }
    _value_list[DateIndex] = x;
}

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

void RainbowNode::PushUnderlying( MonteCarloNode* N )
{
    _underlying_node_list.push_back( N );
}

void RainbowNode::PushWeight( double Weight )
{
    _weight_list.push_back( Weight );
}

void RainbowNode::SetBest( bool Best )
{
    _best = Best;
}
