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
    auto init = [&]( ConstantNode* C )
    { C->SetConstantValue( _rate->GetCurveValue( _today ) ); };
    //! mutualise with the base tree unless the current Greek scenario bumps rates
    if ( NC.HasScenario() && !NC.ScenarioBumpsRate() )
    {
        return NC.GetOrCreateShared<ConstantNode>( _name + "#rate", init );
    }
    return NC.GetOrCreate<ConstantNode>( _name + "#rate", init );
}