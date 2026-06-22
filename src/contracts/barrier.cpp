#include "thoth.hpp"
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
    _strike = reader.Get<double>( "strike" );
    _maturity_date = reader.Get<date>( "maturity" );
    _type = ParseOptionType( reader.Get<string>( "type" ) );
    _barrier_type = ParseBarrierType( reader.Get<string>( "barrier_type" ) );
    _barrier_monitoring_type = ParseBarrierMonitoring( reader.Get<string>( "barrier_monitoring_type" ) );
    _monitoring_period_days = reader.Get<int>( "monitoring_period_days", 0 );
    _barrier_up_level = reader.Get<double>( "barrier_up_level", 0 );
    _barrier_down_level = reader.Get<double>( "barrier_down_level", 0 );
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

//! the PDE prices a single barrier on an equity-like underlying: continuous
//! monitoring by a Dirichlet boundary, discrete monitoring by zeroing the
//! knocked region at the scheduled dates (knock-in by in/out parity).
bool Barrier::PDE_HasSolution()
{
    return _underlying->IsGriddable();
}

//! a Barrier always is one (trivially true; lets the PDE engine branch on type)
bool Barrier::PDE_IsBarrier()
{
    return true;
}

//! knock-in vs knock-out (the engine prices KO and applies in/out parity for KI)
bool Barrier::PDE_IsKnockIn()
{
    return IsKnockIn( _barrier_type );
}

//! up barrier (H above spot) vs down barrier (H below spot)
bool Barrier::PDE_IsUpBarrier()
{
    return IsUpBarrier( _barrier_type );
}

//! the active level H: the up level for an up barrier, else the down level
double Barrier::PDE_BarrierLevel()
{
    return PDE_IsUpBarrier() ? _barrier_up_level : _barrier_down_level;
}

//! a closed-form (Reiner-Rubinstein) solution exists for a single, continuously
//! monitored barrier on an equity-like underlying.
bool Barrier::ANA_HasSolution()
{
    return ( _barrier_monitoring_type == BarrierMonitoring::Continuous ) &&
           _underlying->IsGriddable();
}

//! Reiner-Rubinstein closed-form barrier price for a single spot value: decode this
//! contract's flavour (call/put, down/up, in/out, active level) and delegate to the
//! finance-module formula — mirroring how Vanilla::ANA_EvalPrice calls BS_*_Price.
double Barrier::ANA_BarrierPrice( double S,
                                  double r,
                                  double b,
                                  double v,
                                  double t,
                                  double df )
{
    bool is_call = ( _type == OptionType::Call );
    bool is_down = ( _barrier_type == BarrierType::DownAndOut ||
                     _barrier_type == BarrierType::DownAndIn );
    bool is_in = IsKnockIn( _barrier_type );
    double H = is_down ? _barrier_down_level : _barrier_up_level; //!< active barrier
    return Barrier_Price( S, r, b, v, t, df, H, _strike, is_call, is_down, is_in );
}

//! closed-form barrier pricing (premium + finite-difference spot greeks)
void Barrier::ANA_EvalPrice()
{
    //! market data (same conventions as Vanilla::ANA_EvalPrice)
    double t = YearFraction( _today, _maturity_date );
    double df = _premium_currency->GetRate()->GetDiscountFactor( _maturity_date );
    double f = _underlying->GetForward( _maturity_date, _premium_currency );
    double s = _underlying->GetSpot();
    double v = _underlying->GetImplicitVol( _strike, _maturity_date );

    //! back out the constant r and cost-of-carry b that reproduce the market df and
    //! forward (r from df = e^{-rt}, b from fwd = s e^{bt}); the closed form is
    //! parametrised in (r, b) rather than (df, fwd)
    double r = ( t > 0 ) ? -log( df ) / t : 0.0;
    double b = ( t > 0 && s > 0 ) ? log( f / s ) / t : 0.0;

    //! premium
    _valuation.premium = ANA_BarrierPrice( s, r, b, v, t, df );

    //! delta & gamma by central finite difference on the spot (the closed form has
    //! no clean analytic Greeks across the case table, so bump-and-revalue). The
    //! bump is symmetric (+/- half a bump) so delta is second-order accurate; gamma
    //! divides by the half-bump squared.
    double s_up = s * ( 1 + GREEK_SPOT_BUMP / 2 );
    double s_dw = s * ( 1 - GREEK_SPOT_BUMP / 2 );
    double p_up = ANA_BarrierPrice( s_up, r, b, v, t, df );
    double p_dw = ANA_BarrierPrice( s_dw, r, b, v, t, df );
    _valuation.delta = ( p_up - p_dw ) / ( s * GREEK_SPOT_BUMP );
    _valuation.gamma = ( p_up + p_dw - 2 * _valuation.premium ) /
                       ( s * GREEK_SPOT_BUMP / 2 * s * GREEK_SPOT_BUMP / 2 );
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

            bool is_up = PDE_IsUpBarrier();
            double H = PDE_BarrierLevel();

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
            C->SetIsIn( PDE_IsKnockIn() );
            C->SetMonitorIndexList( monitor );
        } );
}