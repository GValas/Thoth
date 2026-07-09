#include "thoth.hpp"
#include "finance.hpp"
#include "vanilla.hpp"
#include "enums.hpp"
#include "object_reader.hpp"

//! Vanilla option implementation: configuration, schedule and the Monte-Carlo flow
//! node. The analytic closed form and the GPU-GBM gate live in the engines
//! (PricerANA / PricerMCL); the contract is a pure description.

// constructor
Vanilla::Vanilla( const string& ObjectName ) : Contract( ObjectName, KIND_VANILLA )
{
}

Vanilla::~Vanilla() = default;

//! read own fields, then the common contract attributes
void Vanilla::Configure( ObjectReader& reader )
{
    Contract::Configure( reader ); //!< common fields first (underlying, premium currency)
    _strike_input = reader.Get<double>( "strike" );
    _is_absolute_strike = reader.Get<bool>( "is_absolute_strike", true );
    _strike = _strike_input; //!< a relative strike is resolved in SetToday
    _exercise_mode = ParseExerciseMode( reader.Get<string>( "exercise" ) );
    _maturity_date = reader.Get<date>( "maturity" );
    _type = ParseOptionType( reader.Get<string>( "type" ) );
}

//! Resolve the cash strike: a relative strike is a percent of the underlying's
//! spot as of the valuation date (basket/rainbow spots are their rebased-100
//! level, so 100 stays ATM in both conventions). Resolution happens here — the
//! Greek bump engine mutates the spot *without* re-anchoring today — so the cash
//! strike is fixed against the base spot and never follows a bump scenario. The
//! theta roll re-enters with the spot restored, making the resolution idempotent.
void Vanilla::SetToday( const date& Today )
{
    Contract::SetToday( Today );
    _strike = _is_absolute_strike ? _strike_input
                                  : _strike_input / 100 * _underlying->GetSpot();
}

//! getter
double Vanilla::GetStrike() const
{
    return _strike;
}

//! getter
OptionType Vanilla::GetType() const
{
    return _type;
}

//! getter
date Vanilla::GetMaturityDate() const
{
    return _maturity_date;
}

//! single spot fixing at maturity
set<date> Vanilla::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! single cash flow at maturity
set<date> Vanilla::GetFlowDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! terminal call/put payoff max(phi(S-K),0); the trailing payoff_vanilla flags
//! select a plain (un-digital, un-capped) vanilla
double Vanilla::Intrinsic( const double Spot )
{
    return payoff_vanilla( Spot, _strike, _type, false, 0, true, 0 );
}

//! American iff configured so (drives the LSM / PDE early-exercise path)
bool Vanilla::IsAmerican()
{
    return _exercise_mode == ExerciseMode::American;
}

//! Build (or fetch) the Monte-Carlo flow node: a call/put payoff (floored at 0)
//! on the spot node, settling at maturity.
MonteCarloNode* Vanilla::GetFlowNode( NodeCollector& NC,
                                      const date& /*AsOfDate*/ )
{

    return NC.GetOrCreate<VanillaFlowNode>( _name + node_name::FLOW,
                                            [&]( VanillaFlowNode* C )
                                            {
                                                C->SetFloor( 0 );
                                                C->SetType( _type );
                                                C->SetStrike( _strike );
                                                C->SetSpotNode( GetUnderlyingNode( NC ) );
                                                C->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );
                                            } );
}