#include "thoth.hpp"
#include "currency.hpp"

Currency::Currency( const string& ObjectName ) : Object( ObjectName, KIND_CURRENCY )
{
    _rate = nullptr;
}

Currency::~Currency() = default;

//! setter
void Currency::SetRate( YieldCurve& Rate )
{
    _rate = &Rate;
}

//! setter
void Currency::SetToday( const date& Today )
{
    Object::SetToday( Today );
    _rate->SetToday( Today );
}

//! getter
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