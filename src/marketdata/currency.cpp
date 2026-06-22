#include "thoth.hpp"
#include "currency.hpp"
#include "object_reader.hpp"

//! currency.cpp — Currency implementation: curve wiring, dating, MCL rate node.

//! constructor — tag as a currency kind; the rate curve is wired in Configure
Currency::Currency( const string& ObjectName ) : MarketData( ObjectName, KIND_CURRENCY )
{
    _rate = nullptr;
}

Currency::~Currency() = default;

//! read the discount (yield) curve this currency wraps (referenced by the YAML field
//! "rate"); the curve itself is a separate book object resolved here by reference
void Currency::Configure( ObjectReader& reader )
{
    SetRate( *reader.Ref<YieldCurve>( "rate" ) );
}

//! setter — bind the discount/yield curve by address (shared book object, not owned)
void Currency::SetRate( YieldCurve& Rate )
{
    _rate = &Rate;
}

//! setter — date this Object first, then push the same date into the wrapped curve so
//! its discount factors are computed from the right valuation date
void Currency::SetToday( const date& Today )
{
    Object::SetToday( Today );
    _rate->SetToday( Today );
}

//! getter — the wrapped discount/yield curve
YieldCurve* Currency::GetRate() const
{
    return _rate;
}

MonteCarloNode* Currency::GetDiscFactorNode( NodeCollector& /*NC*/ )
{
    //! discount-factor node is not implemented (and currently unused)
    ERR( "currency '" + _name + "' : GetDiscFactorNode not implemented" );
}

MonteCarloNode* Currency::GetRateNode( NodeCollector& NC )
{
    //! term-structured zero-rate node: the drift and the discounting both follow the
    //! whole curve (a flat curve reduces to the previous single-rate behaviour). The
    //! rho bump is carried inside the curve's GetCurveValue, so a bumped scenario
    //! builds its own node (below) over the shifted curve.
    auto init = [&]( YieldCurveNode* Y )
    { Y->SetCurve( _rate ); };
    //! mutualise with the base tree unless the current Greek scenario bumps rates
    if ( NC.HasScenario() && !NC.ScenarioBumpsRate() )
    {
        return NC.GetOrCreateShared<YieldCurveNode>( _name + node_name::RATE, init );
    }
    return NC.GetOrCreate<YieldCurveNode>( _name + node_name::RATE, init );
}