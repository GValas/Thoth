#include "thoth.hpp"
#include "variance_swap.hpp"
#include "object_reader.hpp"

//! Variance-swap implementation: configuration, the model-free static-replication
//! fair-variance (Demeterfi-Derman-Kamal-Zou) closed form, and the Monte-Carlo flow
//! node that compares realized to strike variance.

//! constructor
VarianceSwap::VarianceSwap( const string& ObjectName ) : Contract( ObjectName, KIND_VARIANCE_SWAP )
{
}

VarianceSwap::~VarianceSwap() = default;

//! read own fields, then the common contract attributes
void VarianceSwap::Configure( ObjectReader& reader )
{
    Contract::Configure( reader ); //!< common fields first (underlying, premium currency)
    _maturity_date = reader.Get<date>( "maturity" );
    //! volatility_strike is in percent (like every vol), stored as decimal
    _volatility_strike = reader.Get<double>( "volatility_strike" ) / 100.0;
    _notional = reader.Get<double>( "notional", 1 );
    //! optional discrete fixing schedule (days between observations); 0/absent
    //! keeps the continuous-observation convention (every diffusion step)
    _observation_period_days = reader.Get<int>( "observation_period_days", 0 );
    if ( _observation_period_days < 0 )
    {
        ERR( "variance_swap '" + GetName() + "': observation_period_days must be >= 0" );
    }
}

//! getter
date VarianceSwap::GetMaturityDate() const
{
    return _maturity_date;
}

//! the variance PDE has a zero terminal condition and its own (remaining-variance)
//! boundaries — there is no terminal spot payoff here, so this is just 0.
double VarianceSwap::Intrinsic( const double /*Spot*/ )
{
    return 0.0;
}

//! variance swaps settle only at maturity: no early exercise
bool VarianceSwap::IsAmerican()
{
    return false;
}

//! Monte-Carlo flow: realized variance of the simulated path vs the strike.
//! Build (or fetch) a VarianceSwapFlowNode wired to the spot node, the strike
//! variance (vol^2) and the notional, settling at maturity.
MonteCarloNode* VarianceSwap::GetFlowNode( NodeCollector& NC,
                                           const date& /*AsOfDate*/ )
{
    return NC.GetOrCreate<VarianceSwapFlowNode>(
        _name + node_name::FLOW,
        [&]( VarianceSwapFlowNode* V )
        {
            V->SetSpotNode( GetUnderlyingNode( NC ) );
            V->SetStrikeVariance( _volatility_strike * _volatility_strike );
            V->SetNotional( _notional );
            V->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );
            //! discrete observation: hand the node the fixing-date indices so the
            //! realized variance is sampled on the schedule instead of every step
            //! (the dates are in the grid — GetFixingDates includes them)
            if ( IsDiscretelyObserved() )
            {
                vector<size_t> idx;
                for ( const date& d : GetObservationDates() )
                {
                    idx.push_back( NC.GetDateIndex( d ) );
                }
                V->SetObservationDateIndices( idx );
            }
        } );
}

//! discrete fixing schedule: today + k*period (k>=1) up to, and always including,
//! maturity — the same convention as a discrete barrier's monitoring schedule
set<date> VarianceSwap::GetObservationDates()
{
    set<date> s;
    if ( _observation_period_days > 0 )
    {
        for ( date d = _today + days( _observation_period_days );
              d < _maturity_date;
              d += days( _observation_period_days ) )
        {
            s.insert( d );
        }
    }
    s.insert( _maturity_date );
    return s;
}

//! deterministic drift^2 add-on for a discrete schedule (see the header): the mean
//! log-return of each interval is log(F(t2)/F(t1)) - AtmVol^2/2*dt, squared and
//! summed over the schedule, annualised by T. The forward carries the full curve
//! term structure and the quanto correction, so ANA/PDE stay consistent with the
//! MCL path sampling that produces this term naturally.
double VarianceSwap::ObservationDriftVariance( const date& Today, double AtmVol )
{
    if ( !IsDiscretelyObserved() )
    {
        return 0;
    }
    const double T = YearFraction( Today, _maturity_date );
    if ( T <= 0 )
    {
        return 0;
    }
    Underlying* u = GetUnderlying();
    Currency* ccy = GetPremiumCurrency();
    double sum = 0;
    date d1 = Today;
    double f1 = u->GetForward( d1, ccy ); //!< dt = 0 -> the spot
    for ( const date& d2 : GetObservationDates() )
    {
        const double f2 = u->GetForward( d2, ccy );
        const double dt = YearFraction( d1, d2 );
        const double m = log( f2 / f1 ) - 0.5 * AtmVol * AtmVol * dt;
        sum += m * m;
        d1 = d2;
        f1 = f2;
    }
    return sum / T;
}

//! spot observations the diffusion must produce: the settlement date, plus — for a
//! discretely-observed swap — every fixing date of the observation schedule (so the
//! MCL grid diffuses the spot exactly there). A continuously-observed swap samples
//! every diffusion step and needs only the maturity here.
set<date> VarianceSwap::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    if ( IsDiscretelyObserved() )
    {
        set<date> obs = GetObservationDates();
        s.insert( obs.begin(), obs.end() );
    }
    return s;
}

//! same single date as the fixing
set<date> VarianceSwap::GetFlowDates()
{
    return GetFixingDates();
}
