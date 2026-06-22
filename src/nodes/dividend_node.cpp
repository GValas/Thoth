#include "thoth.hpp"
#include "nodes.hpp"

DividendNode::DividendNode( const string& Name ) : MonteCarloNode( Name )
{
}

DividendNode::~DividendNode() = default;

//! push the future-dividend PV for the next diffusion date; called in date order so
//! the vector index matches the diffusion-date index
void DividendNode::PushFuturePv( double Pv )
{
    _future_pv.push_back( Pv );
}

//! purely deterministic: just surface the precomputed PV for this date (no RNG, no
//! children — the escrow amounts were computed once by Equity from its curve)
void DividendNode::ComputeValue( size_t DateIndex )
{
    _value_list[DateIndex] = _future_pv[DateIndex];
}

//! a precomputed deterministic schedule has no graph children
void DividendNode::GetDateDependencies( size_t /*DateIndex*/,
                                        vector<MonteCarloNode*>& /*NodeList*/,
                                        vector<size_t>& /*DateList*/ )
{
}
