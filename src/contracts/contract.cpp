#include "thoth.hpp"
#include "contract.hpp"
#include "correlation.hpp"
#include "currency.hpp"
#include "object_reader.hpp"

//! Contract base implementation: common configuration (underlying + premium
//! currency resolution), the canonical Monte-Carlo node assembly (contract node =
//! sum of per-date flow nodes) and the quanto-aware underlying spot node. Concrete
//! contracts add their own payoff flow nodes, dates and pricing-facet predicates.

//! constructor (members are initialised in-class)
Contract::Contract( const string& ObjectName,
                    const string& ObjectKind ) : Object( ObjectName, ObjectKind ) {}

//! attributes common to every contract; each concrete Configure calls this base first
void Contract::Configure( ObjectReader& reader )
{
    _underlying = reader.Ref<Underlying>( "underlying" );
    _premium_currency = reader.Ref<Currency>( "premium_currency" );

    //! force underlying currency for a basket / rainbow (its rebased spot is
    //! dimensionless and settled in the premium currency)
    if ( _underlying->GetKind() == KIND_BASKET || _underlying->GetKind() == KIND_RAINBOW )
    {
        _underlying->SetCurrency( _premium_currency );
    }
}

//! destructor
Contract::~Contract() = default;

//! cascade the valuation date: currency (curve roll) and underlying (spot/vol roll)
//! must be re-anchored to Today before the base Object records it
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

//! Build (or fetch the cached) ContractNode: the discounted sum of this contract's
//! cash flows. One flow node per flow date, each keyed by its date index, plus the
//! premium-currency rate curve node used to discount them.
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

//! The spot node the payoff observes. When the underlying settles in a different
//! currency than the premium (a quanto), the spot must be drift-adjusted; otherwise
//! the bare spot node is returned.
MonteCarloNode* Contract::GetUnderlyingNode( NodeCollector& NC )
{
    //! misc: underlying vs contract currency, and a node name that encodes the
    //! quanto target so the quanto-adjusted node never collides with the bare one
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
            //! quanto drift = -rho_{S,FX} * sigma_S * sigma_FX: feed the node the
            //! spot, the spot/FX correlation, the spot vol and the FX vol it needs
            Q->SetUdlSpotNode( _underlying->GetNode( NC ) );
            Q->SetUdlFxCorrelNode( _underlying->GetCorrelNode( NC, udl_ccy, ctr_ccy ) );
            Q->SetUdlVolNode( _underlying->GetVolNode( NC ) );
            Q->SetFxVolNode( _correlation->GetFxVolNode( NC, udl_ccy, ctr_ccy ) );
        } );
}

//! the single names backing this contract (delegated to the underlying)
SingleSet Contract::GetSingleSet() const
{
    return _underlying->GetSingleSet();
}