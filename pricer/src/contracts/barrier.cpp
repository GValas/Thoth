#include "thoth.hpp"
#include "finance.hpp"
#include "barrier.hpp"
#include "enums.hpp"
#include "object_reader.hpp"

//! Barrier option implementation: configuration, monitoring schedule, the
//! Reiner-Rubinstein closed form (continuous monitoring, in/out parity) and the
//! Monte-Carlo flow node (with a Broadie-Glasserman-Kou continuity correction for
//! continuous monitoring on a discrete diffusion grid).

//! constructor — tags the contract with the barrier KIND
Barrier::Barrier( const string& ObjectName ) : Contract( ObjectName, KIND_BARRIER )
{
}

Barrier::~Barrier() = default;

//! read own fields, then the common contract attributes
void Barrier::Configure( ObjectReader& reader )
{
    Contract::Configure( reader ); //!< common fields first (underlying, premium currency)
    _strike_input = reader.Get<double>( "strike" );
    _is_absolute_strike = reader.Get<bool>( "is_absolute_strike", true );
    _strike = _strike_input; //!< a relative strike is resolved in SetToday
    _maturity_date = reader.Get<date>( "maturity" );
    _type = ParseOptionType( reader.Get<string>( "type" ) );
    _barrier_type = ParseBarrierType( reader.Get<string>( "barrier_type" ) );
    _barrier_monitoring_type = ParseBarrierMonitoring( reader.Get<string>( "barrier_monitoring_type" ) );
    _monitoring_period_days = reader.Get<int>( "monitoring_period_days", 0 );
    _barrier_up_level = reader.Get<double>( "barrier_up_level", 0 );
    _barrier_down_level = reader.Get<double>( "barrier_down_level", 0 );
}

//! Resolve the cash strike (see Vanilla::SetToday: same convention — a relative
//! strike is a percent of the base spot, fixed before any Greek bump; barrier
//! levels stay absolute).
void Barrier::SetToday( const date& Today )
{
    Contract::SetToday( Today );
    _strike = _is_absolute_strike ? _strike_input
                                  : _strike_input / 100 * _underlying->GetSpot();
}

//! getter
date Barrier::GetMaturityDate() const
{
    return _maturity_date;
}

//! monitoring schedule for a discrete barrier: today + k*period (k>=1) up to,
//! and always including, maturity. Empty period -> just maturity.
set<date> Barrier::GetMonitoringDates()
{
    set<date> s;
    if ( _monitoring_period_days > 0 )
    {
        for ( date d = _today + days( _monitoring_period_days );
              d < _maturity_date;
              d += days( _monitoring_period_days ) )
        {
            s.insert( d );
        }
    }
    s.insert( _maturity_date );
    return s;
}

//! spot observations the diffusion must produce. Maturity is always needed for the
//! terminal payoff; a discrete barrier additionally needs every monitoring date so
//! the engine can test the level there. (Continuous monitoring uses every diffusion
//! date and so does not enlarge this set.)
set<date> Barrier::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    //! a discrete barrier must be diffused at every monitoring date
    if ( IsDiscrete() )
    {
        set<date> m = GetMonitoringDates();
        s.insert( m.begin(), m.end() );
    }
    return s;
}

//! the only cash flow settles at maturity
set<date> Barrier::GetFlowDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! terminal payoff is the vanilla one (the trailing payoff_vanilla flags select a
//! plain, un-digital, un-capped call/put); the knock-out is enforced by the grid
//! boundary (continuous monitoring), so no special handling is needed here.
double Barrier::Intrinsic( const double spot )
{
    return payoff_vanilla( spot, _strike, _type, false, 0, true, 0 );
}

//! up barrier (H above spot) vs down barrier (H below spot)
bool Barrier::IsUp() const
{
    return IsUpBarrier( _barrier_type );
}

//! knock-in vs knock-out (engines price KO and apply in/out parity for KI)
bool Barrier::IsIn() const
{
    return IsKnockIn( _barrier_type );
}

//! the active level H: the up level for an up barrier, else the down level
double Barrier::Level() const
{
    return IsUp() ? _barrier_up_level : _barrier_down_level;
}

//! Build the Monte-Carlo flow node: a vanilla payoff at maturity, gated by the
//! barrier monitored over a list of diffusion-date indices.
MonteCarloNode* Barrier::GetFlowNode( NodeCollector& NC,
                                      const date& /*AsOfDate*/ )
{

    return NC.GetOrCreate<BarrierFlowNode>(
        _name + node_name::FLOW,
        [&]( BarrierFlowNode* C )
        {
            //! terminal vanilla payoff (floored at 0) observed on the spot node
            C->SetFloor( 0 );
            C->SetType( _type );
            C->SetStrike( _strike );
            C->SetSpotNode( GetUnderlyingNode( NC ) );
            C->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );

            bool is_up = IsUp();
            double H = Level();

            vector<size_t> monitor;
            if ( IsDiscrete() )
            {
                //! monitor exactly at the scheduled dates, no continuity correction
                for ( const date& d : GetMonitoringDates() )
                {
                    monitor.push_back( NC.GetDateIndex( d ) );
                }
            }
            else
            {
                //! continuous: monitor every diffusion date up to maturity, with a
                //! Broadie-Glasserman-Kou continuity correction (the discretely
                //! monitored barrier misses crossings between dates, so the level
                //! is moved towards the spot by exp(-/+ 0.5826 * vol * sqrt(dt))).
                monitor = NC.DiffusionIndicesUpTo( _maturity_date );
                double t = YearFraction( _today, _maturity_date );
                //! average step dt = T / (#monitor dates - 1); guard the single-date case
                size_t steps = ( monitor.size() > 1 ) ? monitor.size() - 1 : 1;
                double dt = t / (double)steps;
                double vol = _underlying->GetImplicitVol( _strike, _maturity_date );
                const double beta = 0.5826; //!< -zeta(1/2)/sqrt(2pi), the BGK constant
                //! shift up barriers DOWN and down barriers UP towards the spot, so the
                //! discrete-grid KO probability matches the continuous one
                H *= exp( ( is_up ? -1.0 : 1.0 ) * beta * vol * sqrt( dt ) );
            }

            //! hand the (possibly corrected) level + flavour + monitoring grid to the node
            C->SetBarrierLevel( H );
            C->SetIsUp( is_up );
            C->SetIsIn( IsIn() );
            C->SetMonitorIndexList( monitor );
        } );
}