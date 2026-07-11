#include "thoth.hpp"
#include "currency.hpp"
#include "hull_white.hpp"
#include "object_reader.hpp"

//! currency.cpp — Currency implementation: curve wiring, dating, MCL rate nodes.

//! constructor — tag as a currency kind; the curves are wired in Configure
Currency::Currency( const string& ObjectName ) : MarketData( ObjectName, KIND_CURRENCY )
{
    _rate = nullptr;
    _discount_rate = nullptr;
    _rate_model = nullptr;
}

Currency::~Currency() = default;

//! read the projection (yield) curve this currency wraps (YAML field "rate") and
//! the optional discounting curve ("discount_rate", the OIS / collateral curve);
//! both are separate book objects resolved by reference. Without "discount_rate"
//! the projection curve discounts too (the historic single-curve behaviour).
void Currency::Configure( ObjectReader& reader )
{
    _rate = reader.Ref<YieldCurve>( "rate" ); //!< shared book object, not owned
    _discount_rate = reader.Has<string>( "discount_rate" )
                         ? reader.Ref<YieldCurve>( "discount_rate" )
                         : _rate;
    //! optional stochastic-rate model (Hull-White 1F) on the discounting rate
    _rate_model = reader.Has<string>( "rate_model" )
                      ? reader.Ref<HullWhite>( "rate_model" )
                      : nullptr;
}

//! setter — date this Object first, then push the same date into the wrapped
//! curve(s) so their discount factors are computed from the right valuation date
void Currency::SetToday( const date& Today )
{
    Object::SetToday( Today );
    _rate->SetToday( Today );
    if ( HasDistinctDiscountCurve() )
    {
        _discount_rate->SetToday( Today );
    }
}

//! getter — the projection / funding curve (forwards and drifts)
YieldCurve* Currency::GetRate() const
{
    return _rate;
}

//! getter — the discounting (OIS) curve; == GetRate() when single-curve
YieldCurve* Currency::GetDiscountRate() const
{
    return _discount_rate;
}

//! true when a distinct discounting curve is configured (multi-curve book)
bool Currency::HasDistinctDiscountCurve() const
{
    return _discount_rate != _rate;
}

//! getter — the optional Hull-White model (null = deterministic rates)
HullWhite* Currency::GetRateModel() const
{
    return _rate_model;
}

//! the correlation-matrix pseudo-single of this currency's rate factor
string Currency::IrFactorName() const
{
    return _name + IR_FACTOR_SUFFIX;
}

//! the Hull-White exponent X(t) = int_0^t x + V(t)/2, built over the correlated
//! noise "<name>_ir#noise" (created by the MCL correlation wiring). Both nodes
//! are SHARED across Greek scenarios: the noises are common random numbers, and
//! no engine Greek bumps the HW parameters (rho bumps the curves, whose z t legs
//! live on the scenario-aware yield-curve nodes).
MonteCarloNode* Currency::GetHwExponentNode( NodeCollector& NC )
{
    if ( !_rate_model )
    {
        ERR( "currency '" + _name + "' : GetHwExponentNode without a rate_model" );
    }
    MonteCarloNode* factor = NC.GetOrCreateShared<HullWhiteFactorNode>(
        IrFactorName() + node_name::IR_FACTOR,
        [&]( HullWhiteFactorNode* F )
        {
            F->SetParameters( _rate_model->A(), _rate_model->Sigma() );
            MonteCarloNode* noise = NC.GetNode( IrFactorName() + node_name::NOISE );
            if ( !noise )
            {
                ERR( "currency '" + _name + "' : correlated rate noise '" + IrFactorName() +
                     node_name::NOISE + "' not built (is '" + IrFactorName() +
                     "' in the correlation matrix?)" );
            }
            F->SetNoiseNode( noise );
        } );
    return NC.GetOrCreateShared<HullWhiteIntegralNode>(
        IrFactorName() + node_name::IR_EXPONENT,
        [&]( HullWhiteIntegralNode* I )
        {
            I->SetParameters( _rate_model->A(), _rate_model->Sigma() );
            I->SetFactorNode( factor );
        } );
}

MonteCarloNode* Currency::GetRateNode( NodeCollector& NC )
{
    //! term-structured zero-rate node: the drift follows the whole projection curve
    //! (a flat curve reduces to the previous single-rate behaviour). The rho bump is
    //! carried inside the curve's GetCurveValue, so a bumped scenario builds its own
    //! node (below) over the shifted curve.
    auto init = [&]( YieldCurveNode* Y )
    { Y->SetCurve( _rate ); };
    //! mutualise with the base tree unless the current Greek scenario bumps rates
    if ( NC.Scenario().active() && !NC.Scenario().bumps_rate )
    {
        return NC.GetOrCreateShared<YieldCurveNode>( _name + node_name::RATE, init );
    }
    return NC.GetOrCreate<YieldCurveNode>( _name + node_name::RATE, init );
}

//! the discounting leg: the same term-structured zero-rate node pattern over the
//! OIS curve. Single-curve currencies return the projection node itself, so the
//! graph (and every historic book) is unchanged.
MonteCarloNode* Currency::GetDiscountRateNode( NodeCollector& NC )
{
    if ( !HasDistinctDiscountCurve() )
    {
        return GetRateNode( NC );
    }
    auto init = [&]( YieldCurveNode* Y )
    { Y->SetCurve( _discount_rate ); };
    if ( NC.Scenario().active() && !NC.Scenario().bumps_rate )
    {
        return NC.GetOrCreateShared<YieldCurveNode>( _name + node_name::DISC_RATE, init );
    }
    return NC.GetOrCreate<YieldCurveNode>( _name + node_name::DISC_RATE, init );
}
