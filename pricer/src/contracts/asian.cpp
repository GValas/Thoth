#include "thoth.hpp"
#include "asian.hpp"
#include "object_reader.hpp"

//! Asian (arithmetic average-price) option: configuration, the averaging
//! schedule, and the Monte-Carlo flow node (see asian_flow_node).

Asian::Asian( const string& ObjectName ) : Contract( ObjectName, KIND_ASIAN )
{
}

Asian::~Asian() = default;

//! read own fields, then the common contract attributes
void Asian::Configure( ObjectReader& reader )
{
    Contract::Configure( reader ); //!< common fields first (underlying, premium currency)
    _maturity_date = reader.Get<date>( "maturity" );
    _type = ParseOptionType( reader.Get<string>( "type" ) );
    _strike_input = reader.Get<double>( "strike" );
    _is_absolute_strike = reader.Get<bool>( "is_absolute_strike", true );
    _strike = _strike_input; //!< a relative strike is resolved in SetToday
    _nominal = reader.Get<double>( "nominal", 1 );
    //! averaging schedule: days between fixings — monthly by default, must be > 0
    _observation_period_days = reader.Get<int>( "observation_period_days", 30 );
    if ( _observation_period_days <= 0 )
    {
        ERR( "asian '" + GetName() + "': observation_period_days must be > 0" );
    }
}

//! resolve the cash strike: a relative strike is a percent of the underlying's
//! spot as of the valuation date. Resolution happens here — the Greek bump engine
//! mutates the spot without re-anchoring today — so the cash strike is fixed
//! against the base spot and never follows a bump scenario (sticky-cash); the
//! theta roll re-enters with the spot restored, making it idempotent.
void Asian::SetToday( const date& Today )
{
    Contract::SetToday( Today );
    _strike = _is_absolute_strike ? _strike_input
                                  : _strike_input / 100 * _underlying->GetSpot();
}

//! getter
date Asian::GetMaturityDate() const
{
    return _maturity_date;
}

//! the averaging schedule: anchor (today) + k*period (k >= 1) up to, and always
//! including, maturity — the same convention as the variance-swap / barrier
//! monitoring schedule. Averaging starts after inception (the first fixing is one
//! period out), so today's spot is NOT part of the average.
set<date> Asian::GetObservationDates() const
{
    set<date> s;
    for ( date d = _today + days( _observation_period_days );
          d < _maturity_date;
          d += days( _observation_period_days ) )
    {
        s.insert( d );
    }
    s.insert( _maturity_date );
    return s;
}

//! no terminal-spot intrinsic (the payoff is on the path average, not S_T); only
//! the PDE would read this, and the PDE rejects the Asian, so 0 is safe.
double Asian::Intrinsic( const double /*spot*/ )
{
    return 0.0;
}

//! automatic settlement at maturity — no early exercise
bool Asian::IsAmerican()
{
    return false;
}

//! spot observations the diffusion must produce: the averaging schedule (which
//! always includes maturity)
set<date> Asian::GetFixingDates()
{
    return GetObservationDates();
}

//! single payment at maturity
set<date> Asian::GetFlowDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! Monte-Carlo flow: the average-price payoff over the schedule.
MonteCarloNode* Asian::GetFlowNode( NodeCollector& NC,
                                    const date& /*AsOfDate*/ )
{
    return NC.GetOrCreate<AsianFlowNode>(
        _name + node_name::FLOW,
        [&]( AsianFlowNode* A )
        {
            A->SetSpotNode( GetUnderlyingNode( NC ) );
            A->SetStrike( _strike );
            A->SetType( _type );
            A->SetNotional( _nominal );
            A->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );
            vector<size_t> idx;
            for ( const date& d : GetObservationDates() )
            {
                idx.push_back( NC.GetDateIndex( d ) );
            }
            A->SetObservationDateIndices( idx );
        } );
}
