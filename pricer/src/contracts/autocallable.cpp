#include "thoth.hpp"
#include "autocallable.hpp"
#include "object_reader.hpp"

//! Autocallable (Athena) implementation: configuration and validation, the
//! sticky-cash level resolution, the maturity redemption profile, and the
//! Monte-Carlo flow nodes (one per schedule date; see autocallable_flow_node).

//! constructor
Autocallable::Autocallable( const string& ObjectName ) : Contract( ObjectName, KIND_AUTOCALLABLE )
{
}

Autocallable::~Autocallable() = default;

//! read own fields, then the common contract attributes
void Autocallable::Configure( ObjectReader& reader )
{
    Contract::Configure( reader ); //!< common fields first (underlying, premium currency)
    _maturity_date = reader.Get<date>( "maturity" );
    _autocall_dates = reader.Get<vector<date>>( "autocall_dates" );
    //! levels and coupon follow the engine-wide percent convention
    _autocall_pct = reader.Get<double>( "autocall_barrier" );
    _protection_pct = reader.Get<double>( "protection_barrier" );
    _coupon = reader.Get<double>( "coupon" ) / 100.0;
    _nominal = reader.Get<double>( "nominal", 100 );

    if ( _autocall_dates.empty() )
    {
        ERR( "autocallable '" + GetName() + "': autocall_dates must not be empty" );
    }
    for ( size_t i = 0; i < _autocall_dates.size(); i++ )
    {
        if ( i > 0 && _autocall_dates[i] <= _autocall_dates[i - 1] )
        {
            ERR( "autocallable '" + GetName() + "': autocall_dates must be strictly increasing" );
        }
        if ( _autocall_dates[i] >= _maturity_date )
        {
            ERR( "autocallable '" + GetName() + "': autocall_dates must precede maturity "
                                                "(the maturity observation is the redemption)" );
        }
    }
    if ( _autocall_pct <= 0 || _protection_pct < 0 || _protection_pct > _autocall_pct )
    {
        ERR( "autocallable '" + GetName() + "': needs 0 <= protection_barrier <= "
                                            "autocall_barrier and autocall_barrier > 0" );
    }
    if ( _coupon < 0 )
    {
        ERR( "autocallable '" + GetName() + "': coupon must be non-negative" );
    }

    //! Phoenix flavour: a per-period conditional coupon barrier, conventionally
    //! at or below the autocall level (the redemption date's coupon is then
    //! implied); the optional memory recovers consecutively missed coupons
    _is_phoenix = reader.Has<double>( "coupon_barrier" );
    if ( _is_phoenix )
    {
        _coupon_barrier_pct = reader.Get<double>( "coupon_barrier" );
        if ( _coupon_barrier_pct < 0 || _coupon_barrier_pct > _autocall_pct )
        {
            ERR( "autocallable '" + GetName() + "': needs 0 <= coupon_barrier <= "
                                                "autocall_barrier" );
        }
    }
    _coupon_memory = reader.Get<bool>( "coupon_memory", false );
    if ( _coupon_memory && !_is_phoenix )
    {
        ERR( "autocallable '" + GetName() + "': coupon_memory needs a coupon_barrier "
                                            "(Phoenix flavour)" );
    }
}

//! resolve the percent levels into cash amounts against the valuation-date spot
//! (the rebased 100 for a basket). Resolution happens here — the Greek bump
//! engine mutates the spot WITHOUT re-anchoring today — so the cash levels are
//! fixed against the base spot and never follow a bump scenario (sticky-cash,
//! the relative-strike convention); the theta roll re-enters with the spot
//! restored, making the resolution idempotent. Also rejects a seasoned schedule
//! (autocall dates strictly before today), which would need past fixings: TODO.
void Autocallable::SetToday( const date& Today )
{
    Contract::SetToday( Today );
    //! an observation falling exactly ON today is observed at the live spot (the
    //! MCL flow reads index 0; the PDE folds it into the first step) — this is
    //! what the theta roll turns a tomorrow-observation into. Only dates strictly
    //! in the past are rejected (seasoned autocallables: TODO).
    if ( _autocall_dates.front() < Today )
    {
        ERR( "autocallable '" + GetName() + "': autocall_dates must not precede today "
                                            "(seasoned autocallables are not supported yet)" );
    }
    const double spot = _underlying->GetSpot();
    _reference_spot = spot;
    _autocall_level = _autocall_pct / 100 * spot;
    _protection_level = _protection_pct / 100 * spot;
    _coupon_level = _is_phoenix ? _coupon_barrier_pct / 100 * spot : 0;
}

//! getter
date Autocallable::GetMaturityDate() const
{
    return _maturity_date;
}

//! early-redemption amount at the k-th observation (1-based): the Athena
//! snowball N * (1 + k * c). A Phoenix redeems at N + ONE period coupon (the
//! redemption date's own coupon flow — S >= autocall >= coupon level implies
//! the condition — while the earlier coupons detached at their own dates).
double Autocallable::Rebate( size_t Position ) const
{
    if ( _is_phoenix )
    {
        return _nominal + PeriodCoupon();
    }
    return _nominal * ( 1 + (double)Position * _coupon );
}

//! the maturity redemption — also the PDE terminal condition and (evaluated at
//! the far grid nodes) its Dirichlet boundaries. Athena: observation n+1 pays
//! the full accrued coupon above the autocall level, the bare nominal above the
//! protection level, the linear capital loss S/S_ref below. Phoenix: nominal /
//! linear redemption plus the TERMINAL period coupon above the coupon level
//! (memory catch-up is path-dependent and lives in the flow node; the PDE
//! rejects the memory flavour).
double Autocallable::Intrinsic( const double Spot )
{
    if ( _is_phoenix )
    {
        const double redemption = ( Spot >= _protection_level )
                                      ? _nominal
                                      : _nominal * Spot / _reference_spot;
        return redemption + ( ( Spot >= _coupon_level ) ? PeriodCoupon() : 0 );
    }
    if ( Spot >= _autocall_level )
    {
        return Rebate( _autocall_dates.size() + 1 );
    }
    if ( Spot >= _protection_level )
    {
        return _nominal;
    }
    return _nominal * Spot / _reference_spot;
}

//! automatic trigger, not an optimal exercise: European-style for every engine
bool Autocallable::IsAmerican()
{
    return false;
}

//! spot observations: every autocall date plus the maturity (all forced into the
//! MCL diffusion grid)
set<date> Autocallable::GetFixingDates()
{
    set<date> s( _autocall_dates.begin(), _autocall_dates.end() );
    s.insert( _maturity_date );
    return s;
}

//! each observation date can pay (the early redemption), and maturity always
//! settles: payment dates == fixing dates
set<date> Autocallable::GetFlowDates()
{
    return GetFixingDates();
}

//! Monte-Carlo flow for ONE schedule date: the node pays this date's cash flow
//! (the accrued rebate if the FIRST autocall trigger is exactly here, the
//! redemption profile at maturity if never triggered) and 0 otherwise. Keyed by
//! the date so the collector holds one node per schedule entry; they share the
//! spot path, and the ContractNode discounts each from its own payment date
//! (pathwise under Hull-White).
MonteCarloNode* Autocallable::GetFlowNode( NodeCollector& NC,
                                           const date& AsOfDate )
{
    const bool is_maturity = ( AsOfDate == _maturity_date );
    //! this flow's 1-based observation position (maturity = n+1, for the coupon)
    size_t position = _autocall_dates.size() + 1;
    if ( !is_maturity )
    {
        for ( size_t i = 0; i < _autocall_dates.size(); i++ )
        {
            if ( _autocall_dates[i] == AsOfDate )
            {
                position = i + 1;
                break;
            }
        }
        if ( position == _autocall_dates.size() + 1 )
        {
            ERR( "autocallable '" + GetName() + "': flow date is not on the schedule" );
        }
    }

    return NC.GetOrCreate<AutocallableFlowNode>(
        _name + node_name::FLOW + node_name::SEP + boost::gregorian::to_iso_string( AsOfDate ),
        [&]( AutocallableFlowNode* A )
        {
            A->SetSpotNode( GetUnderlyingNode( NC ) );
            A->SetFlowDateIndex( NC.GetDateIndex( AsOfDate ) );
            A->SetPosition( position, is_maturity );
            A->SetLevels( _autocall_level, _protection_level, _reference_spot );
            A->SetPayout( _nominal, _coupon );
            if ( _is_phoenix )
            {
                A->SetPhoenix( _coupon_level, _coupon_memory );
            }
            //! the autocall observations this flow must inspect: every schedule
            //! date up to and including its own (only the PRIOR ones at maturity)
            vector<size_t> idx;
            for ( size_t i = 0; i < _autocall_dates.size() && i < position; i++ )
            {
                idx.push_back( NC.GetDateIndex( _autocall_dates[i] ) );
            }
            A->SetAutocallIndexList( idx );
        } );
}
