#include "thoth.hpp"
#include "contract.hpp"
#include "correlation.hpp"
#include "currency.hpp"
#include "object_reader.hpp"

//! constructor (members are initialised in-class)
Contract::Contract( const string& ObjectName,
                    const string& ObjectKind ) : Object( ObjectName, ObjectKind ) {}

//! attributes common to every contract (resolved after the concrete part)
void Contract::ConfigureCommon( ObjectReader& reader )
{
    SetUnderlying( *reader.Ref<Underlying>( "underlying" ) );
    SetPremiumCurrency( *reader.Ref<Currency>( "premium_currency" ) );

    //! force underlying currency for a basket / rainbow (its rebased spot is
    //! dimensionless and settled in the premium currency)
    if ( _underlying->GetKind() == KIND_BASKET || _underlying->GetKind() == KIND_RAINBOW )
    {
        _underlying->SetCurrency( *_premium_currency );
    }
}

//! destructor
Contract::~Contract() = default;

//! setter
void Contract::SetUnderlying( Underlying& Underlying )
{
    _underlying = &Underlying;
}

//! setter
void Contract::SetPremiumCurrency( Currency& PremiumCurrency )
{
    _premium_currency = &PremiumCurrency;
}

//! setter
void Contract::SetToday( const date& Today )
{
    _premium_currency->SetToday( Today );
    _underlying->SetToday( Today );
    Object::SetToday( Today );
}

//! getter
Underlying* Contract::GetUnderlying() const
{
    return _underlying;
}

//! getter
Currency* Contract::GetPremiumCurrency() const
{
    return _premium_currency;
}

//! setter
void Contract::SetCorrelation( Correlation* Correlation )
{
    _correlation = Correlation;
}

MonteCarloNode* Contract::GetNode( NodeCollector& NC )
{
    //! option node is a sum-product over its flow nodes
    return NC.GetOrCreate<ContractNode>(
        _name,
        [&]( ContractNode* C )
        {
            for ( const date& d : GetFlowDates() )
            {
                C->PushFlowNode( GetFlowNode( NC, d ), NC.GetDateIndex( d ) );
            }
            C->SetRateCurveNode( _premium_currency->GetRateNode( NC ) );
        } );
}

MonteCarloNode* Contract::GetUnderlyingNode( NodeCollector& NC )
{
    //! misc
    string udl_ccy = _underlying->GetCurrency()->GetName();
    string ctr_ccy = _premium_currency->GetName();
    string node_name = _underlying->GetName() + node_name::SPOT;
    node_name += ( udl_ccy != ctr_ccy ) ? node_name::QUANTO_PREFIX + ctr_ccy : "";

    //! non-quanto : the bare spot node
    if ( udl_ccy == ctr_ccy )
    {
        return _underlying->GetNode( NC );
    }

    //! quanto : wrap the spot node in a quanto-adjustment node. GetOrCreate keys
    //! by the current scenario, so a Greek-bump sub-tree builds (and shares
    //! within itself) its own quanto node instead of aliasing the base one.
    return NC.GetOrCreate<QuantoAdjustmentNode>(
        node_name,
        [&]( QuantoAdjustmentNode* Q )
        {
            Q->SetUdlSpotNode( _underlying->GetNode( NC ) );
            Q->SetUdlFxCorrelNode( _underlying->GetCorrelNode( NC, udl_ccy, ctr_ccy ) );
            Q->SetUdlVolNode( _underlying->GetVolNode( NC ) );
            Q->SetFxVolNode( _correlation->GetFxVolNode( NC, udl_ccy, ctr_ccy ) );
        } );
}

SingleSet Contract::GetSingleSet() const
{
    return _underlying->GetSingleSet();
}