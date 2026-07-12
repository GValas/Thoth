#include "thoth.hpp"
#include "ratchet.hpp"
#include "object_reader.hpp"

//! Ratchet (cliquet) note: configuration, the period-boundary schedule, and the
//! Monte-Carlo flow node (see ratchet_flow_node).

Ratchet::Ratchet( const string& ObjectName ) : Contract( ObjectName, KIND_RATCHET )
{
}

Ratchet::~Ratchet() = default;

//! read own fields, then the common contract attributes. Floors/caps are in
//! percent (like every rate/level) and stored as decimals.
void Ratchet::Configure( ObjectReader& reader )
{
    Contract::Configure( reader ); //!< common fields first (underlying, premium currency)
    _maturity_date = reader.Get<date>( "maturity" );
    _nominal = reader.Get<double>( "nominal", 100 );
    _observation_period_days = reader.Get<int>( "observation_period_days", 30 );
    if ( _observation_period_days <= 0 )
    {
        ERR( "ratchet '" + GetName() + "': observation_period_days must be > 0" );
    }
    _local_floor = reader.Get<double>( "local_floor" ) / 100.0;
    _local_cap = reader.Get<double>( "local_cap" ) / 100.0;
    if ( _local_cap < _local_floor )
    {
        ERR( "ratchet '" + GetName() + "': local_cap must be >= local_floor" );
    }
    _global_floor = reader.Get<double>( "global_floor", 0.0 ) / 100.0;
    _has_global_cap = reader.Has<double>( "global_cap" );
    if ( _has_global_cap )
    {
        _global_cap = reader.Get<double>( "global_cap" ) / 100.0;
        if ( _global_cap < _global_floor )
        {
            ERR( "ratchet '" + GetName() + "': global_cap must be >= global_floor" );
        }
    }
}

//! getter
date Ratchet::GetMaturityDate() const
{
    return _maturity_date;
}

//! the period-boundary schedule: today (the first S_0), then anchor + k*period up
//! to, and always including, maturity. Consecutive dates bound each period return.
set<date> Ratchet::GetObservationDates() const
{
    set<date> s;
    s.insert( _today ); //!< the first boundary (S_0 = the inception spot)
    for ( date d = _today + days( _observation_period_days );
          d < _maturity_date;
          d += days( _observation_period_days ) )
    {
        s.insert( d );
    }
    s.insert( _maturity_date );
    return s;
}

//! no terminal-spot intrinsic (the payoff is on the summed period returns); only
//! the PDE would read this, and the PDE rejects the ratchet, so 0 is safe.
double Ratchet::Intrinsic( const double /*spot*/ )
{
    return 0.0;
}

//! automatic settlement at maturity — no early exercise
bool Ratchet::IsAmerican()
{
    return false;
}

//! spot observations the diffusion must produce: the boundary schedule
set<date> Ratchet::GetFixingDates()
{
    return GetObservationDates();
}

//! single payment at maturity
set<date> Ratchet::GetFlowDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! Monte-Carlo flow: the clipped-return sum over the boundary schedule.
MonteCarloNode* Ratchet::GetFlowNode( NodeCollector& NC,
                                      const date& /*AsOfDate*/ )
{
    return NC.GetOrCreate<RatchetFlowNode>(
        _name + node_name::FLOW,
        [&]( RatchetFlowNode* R )
        {
            R->SetSpotNode( GetUnderlyingNode( NC ) );
            R->SetNotional( _nominal );
            R->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );
            R->SetLocalClip( _local_floor, _local_cap );
            R->SetGlobalClip( _global_floor, _has_global_cap, _global_cap );
            vector<size_t> idx;
            for ( const date& d : GetObservationDates() )
            {
                idx.push_back( NC.GetDateIndex( d ) );
            }
            R->SetObservationDateIndices( idx );
        } );
}
