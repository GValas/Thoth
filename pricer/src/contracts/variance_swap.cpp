#include "thoth.hpp"
#include "variance_swap.hpp"
#include "object_reader.hpp"
#include "simple_fixing_data.hpp"

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
    //! seasoned (in-life) swap: observation start in the past + realised fixings
    //! (presence probed as a string: dates are scalar fields in the YAML)
    _has_start = reader.Has<string>( "start" );
    if ( _has_start )
    {
        _start_date = reader.Get<date>( "start" );
        if ( _start_date >= _maturity_date )
        {
            ERR( "variance_swap '" + GetName() + "': start must precede maturity" );
        }
    }
    if ( reader.Has<string>( "fixings" ) )
    {
        _fixings = reader.Ref<SimpleFixingData>( "fixings" );
        if ( !_has_start )
        {
            ERR( "variance_swap '" + GetName() + "': fixings need a start date" );
        }
    }
}

//! reject a forward start as soon as the valuation date is known: a start
//! strictly after today would need a forward-starting variance leg none of the
//! engines produces yet. Also anchor the earliest valuation date (kept as a
//! minimum across the theta roll-and-restore), where the realised path is frozen.
void VarianceSwap::SetToday( const date& Today )
{
    Contract::SetToday( Today );
    if ( !_anchor_today_set || Today < _anchor_today )
    {
        _anchor_today = Today;
        _anchor_today_set = true;
    }
    if ( _has_start && _start_date > Today )
    {
        ERR( "variance_swap '" + GetName() +
             "': forward-starting swaps (start after today) are not supported" );
    }
}

//! annualizer of the whole observation window (start -> maturity for a seasoned
//! swap, today -> maturity otherwise)
double VarianceSwap::GetTotalYearFraction() const
{
    return YearFraction( IsSeasoned() ? _start_date : _today, _maturity_date );
}

//! the validated past observations of a seasoned swap, in date order. Discrete
//! observation demands a fixing for EVERY past schedule date (start + k*period
//! up to today, start included); continuous observation takes every provided
//! fixing in [start, today) and demands one at start. Fixings dated today or
//! later are ignored: today's level is the (possibly bumped) live spot.
vector<std::pair<date, double>> VarianceSwap::PastObservations()
{
    if ( !_fixings )
    {
        ERR( "variance_swap '" + GetName() + "': a seasoned swap (start before today) "
                                             "needs a fixings reference" );
    }
    if ( _fixings->GetUnderlying() != GetUnderlying()->GetName() )
    {
        ERR( "variance_swap '" + GetName() + "': fixings '" + _fixings->GetName() +
             "' belong to underlying '" + _fixings->GetUnderlying() + "', not '" +
             GetUnderlying()->GetName() + "'" );
    }

    //! index the provided fixings by date (exact-match lookup)
    const vector<date> dates = _fixings->GetDateList();
    la_vector* values = _fixings->GetValueList();
    map<date, double> by_date;
    for ( size_t i = 0; i < dates.size(); i++ )
    {
        by_date[dates[i]] = la_vector_get( values, i );
    }

    vector<std::pair<date, double>> past;
    if ( IsDiscretelyObserved() )
    {
        for ( date d = _start_date; d < _today && d < _maturity_date;
              d += days( _observation_period_days ) )
        {
            //! a schedule date at/after the anchor valuation date only becomes
            //! "past" through the theta roll (today + 1, spot held): the realised
            //! path is frozen at the anchor, so it fixes at the live spot — which
            //! also zeroes the bridge over the rolled interval, as it should
            if ( _anchor_today_set && d >= _anchor_today )
            {
                past.emplace_back( d, GetUnderlying()->GetSpot() );
                continue;
            }
            auto it = by_date.find( d );
            if ( it == by_date.end() )
            {
                ERR( "variance_swap '" + GetName() + "': missing fixing on " +
                     boost::gregorian::to_iso_extended_string( d ) );
            }
            past.emplace_back( d, it->second );
        }
    }
    else
    {
        for ( const auto& [d, v] : by_date ) //!< map iteration is date-ordered
        {
            if ( d >= _start_date && d < _today )
            {
                past.emplace_back( d, v );
            }
        }
        if ( past.empty() || past.front().first != _start_date )
        {
            ERR( "variance_swap '" + GetName() + "': fixings must include the start date" );
        }
    }
    return past;
}

//! the realised (past) leg of a seasoned swap: squared log-returns over the past
//! observations, closed by the bridge log^2( spot / last_fixing ) — the realised
//! part of the interval running through today.
double VarianceSwap::PastSumSquaredReturns()
{
    if ( !IsSeasoned() )
    {
        return 0;
    }
    const vector<std::pair<date, double>> past = PastObservations();
    double sum2 = 0;
    for ( size_t i = 1; i < past.size(); i++ )
    {
        const double r = log( past[i].second / past[i - 1].second );
        sum2 += r * r;
    }
    const double bridge = log( GetUnderlying()->GetSpot() / past.back().second );
    sum2 += bridge * bridge;
    return sum2;
}

//! the bridge log-return log( spot / last_past_fixing ): the only spot-sensitive
//! term of the realised leg (its analytic delta/gamma feed the ANA/PDE results)
double VarianceSwap::LastFixingLogBridge()
{
    if ( !IsSeasoned() )
    {
        return 0;
    }
    return log( GetUnderlying()->GetSpot() / PastObservations().back().second );
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
            //! seasoned: the realised past leg is a constant added to the simulated
            //! future sum, and the annualizer covers the whole window from start
            if ( IsSeasoned() )
            {
                V->SetPastVariance( PastSumSquaredReturns() );
                V->SetTotalYearFraction( GetTotalYearFraction() );
            }
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

//! discrete fixing schedule: anchor + k*period (k>=1) up to, and always
//! including, maturity — the same convention as a discrete barrier's monitoring
//! schedule. A seasoned swap anchors on its start date (so the remaining
//! schedule stays aligned with the past one) and returns only the FUTURE dates:
//! past observations come from the fixings, not the diffusion.
set<date> VarianceSwap::GetObservationDates()
{
    set<date> s;
    if ( _observation_period_days > 0 )
    {
        const date anchor = IsSeasoned() ? _start_date : _today;
        for ( date d = anchor + days( _observation_period_days );
              d < _maturity_date;
              d += days( _observation_period_days ) )
        {
            if ( d > _today )
            {
                s.insert( d );
            }
        }
    }
    s.insert( _maturity_date );
    return s;
}

//! deterministic drift^2 add-on for a discrete schedule (see the header): the mean
//! log-return of each interval is log(F(t2)/F(t1)) - v_fwd/2 with v_fwd the
//! interval's forward ATM implied variance, squared and summed over the schedule,
//! annualised by T. The forward carries the full curve term structure and the
//! quanto correction, and the per-interval forward variance follows the ATM term
//! structure (a single maturity-ATM vol would misprice each interval's convexity
//! on a sloped surface), so ANA/PDE stay consistent with the MCL path sampling
//! that produces this term naturally. Flat surface: v_fwd = AtmVol^2*dt, the old
//! behaviour, exactly.
double VarianceSwap::ObservationDriftVariance( const date& Today )
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
    double f1 = u->GetForward( Today, ccy ); //!< dt = 0 -> the spot
    double v1 = 0;                           //!< cumulative ATM implied variance sigma^2(t) t
    for ( const date& d2 : GetObservationDates() )
    {
        const double f2 = u->GetForward( d2, ccy );
        const double t2 = YearFraction( Today, d2 );
        const double atm2 = u->GetImplicitVol( f2, d2 );
        const double v2 = atm2 * atm2 * t2;
        //! forward variance of the interval, floored at 0 (a decreasing cumulative
        //! implied variance would mean calendar arbitrage in the quotes)
        const double v_fwd = std::max( 0.0, v2 - v1 );
        const double m = log( f2 / f1 ) - 0.5 * v_fwd;
        sum += m * m;
        f1 = f2;
        v1 = v2;
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
