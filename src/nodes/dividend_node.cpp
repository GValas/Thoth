#include "thoth.hpp"
#include "nodes.hpp"

DividendNode::DividendNode( const string& Name ) : MonteCarloNode( Name )
{
}

DividendNode::~DividendNode() = default;

void DividendNode::PushFuturePv( double Pv )
{
    _future_pv.push_back( Pv );
}

void DividendNode::ComputeValue( size_t DateIndex )
{
    _value_list[DateIndex] = _future_pv[DateIndex];
}

void DividendNode::GetDateDependencies( size_t /*DateIndex*/,
                                        vector<MonteCarloNode*>& /*NodeList*/,
                                        vector<size_t>& /*DateList*/ )
{
}
