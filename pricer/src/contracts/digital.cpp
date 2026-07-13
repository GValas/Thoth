#include "thoth.hpp"
#include "finance.hpp"
#include "digital.hpp"
#include "enums.hpp"
#include "object_reader.hpp"

//! Digital (binary) option implementation: configuration, schedule and the Monte-Carlo
//! flow node. The analytic closed form lives in the engine (PricerANA::PriceDigital); the
//! contract is a pure description. The payoff itself is payoff_digital_option (finance.hpp),
//! shared with the flow node so the MC and analytic legs cannot drift apart.

// constructor
Digital::Digital( const string& ObjectName ) : Contract( ObjectName, KIND_DIGITAL )
{
}

Digital::~Digital() = default;

//! read own fields, then the common contract attributes
void Digital::Configure( ObjectReader& reader )
{
    Contract::Configure( reader ); //!< common fields first (underlying, premium currency)
    _strike_input = reader.Get<double>( "strike" );
    _is_absolute_strike = reader.Get<bool>( "is_absolute_strike", true );
    _strike = _strike_input; //!< a relative strike is resolved in SetToday
    _maturity_date = reader.Get<date>( "maturity" );
    _type = ParseOptionType( reader.Get<string>( "type" ) );

    //! payout style: cash-or-nothing pays a fixed cash amount, asset-or-nothing pays the spot
    const string payout = reader.Get<string>( "payout", "cash_or_nothing" );
    if ( payout != "cash_or_nothing" && payout != "asset_or_nothing" )
    {
        ERR( "digital '" + _name +
             "' : payout must be 'cash_or_nothing' or 'asset_or_nothing'" );
    }
    _is_cash = ( payout == "cash_or_nothing" );
    _cash_amount = reader.Get<double>( "cash_amount", 1 ); //!< Q, cash-or-nothing only
}

//! Resolve the cash strike: a relative strike is a percent of the underlying's spot as of
//! the valuation date, fixed against the base spot so a Greek bump never re-anchors it
//! (same convention as Vanilla::SetToday).
void Digital::SetToday( const date& Today )
{
    Contract::SetToday( Today );
    _strike = _is_absolute_strike ? _strike_input
                                  : _strike_input / 100 * _underlying->GetSpot();
}

//! getters
double Digital::GetStrike() const
{
    return _strike;
}
OptionType Digital::GetType() const
{
    return _type;
}
bool Digital::IsCashOrNothing() const
{
    return _is_cash;
}
double Digital::GetCashAmount() const
{
    return _cash_amount;
}
date Digital::GetMaturityDate() const
{
    return _maturity_date;
}

//! single spot fixing at maturity
set<date> Digital::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! single cash flow at maturity
set<date> Digital::GetFlowDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! terminal binary payoff: cash-or-nothing pays Q, asset-or-nothing pays S_T, iff ITM
double Digital::Intrinsic( const double Spot )
{
    return payoff_digital_option( Spot, _strike, _type, _is_cash, _cash_amount );
}

//! European only
bool Digital::IsAmerican()
{
    return false;
}

//! Build (or fetch) the Monte-Carlo flow node: the binary payoff on the spot node,
//! settling at maturity.
MonteCarloNode* Digital::GetFlowNode( NodeCollector& NC,
                                      const date& /*AsOfDate*/ )
{
    return NC.GetOrCreate<DigitalFlowNode>( _name + node_name::FLOW,
                                            [&]( DigitalFlowNode* C )
                                            {
                                                C->SetType( _type );
                                                C->SetStrike( _strike );
                                                C->SetCashOrNothing( _is_cash );
                                                C->SetCashAmount( _cash_amount );
                                                C->SetSpotNode( GetUnderlyingNode( NC ) );
                                                C->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );
                                            } );
}
