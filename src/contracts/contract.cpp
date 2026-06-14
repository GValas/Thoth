#include "thoth.hpp"
#include "contract.hpp"

//! constructor (members are initialised in-class)
Contract::Contract( const string& ObjectName,
                    const string& ObjectKind ) : Object( ObjectName, ObjectKind ) {}

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
void Contract::SetPremium( double Premium )
{
    _premium = Premium;
}

//! setter
void Contract::SetPremiumTrust( double PremiumTrust )
{
    _premium_trust = PremiumTrust;
}

//! setter
void Contract::SetDelta( double Delta )
{
    _delta = Delta;
}

//! setter
void Contract::SetGamma( double Gamma )
{
    _gamma = Gamma;
}

//! setter
void Contract::SetVega( double Vega )
{
    _vega = Vega;
}

//! setter
void Contract::SetRho( double Rho )
{
    _rho = Rho;
}

//! setter
void Contract::SetTheta( double Theta )
{
    _theta = Theta;
}

//! setter
void Contract::SetToday( const date& Today )
{
    _premium_currency->SetToday( Today );
    _underlying->SetToday( Today );
    Object::SetToday( Today );
}

//! getter
Underlying* Contract::GetUnderlying()
{
    return _underlying;
}

//! getter
Currency* Contract::GetPremiumCurrency()
{
    return _premium_currency;
}

//! getter
double Contract::GetPremium()
{
    return _premium;
}

//! getter
double Contract::GetPremiumTrust()
{
    return _premium_trust;
}

//! getter
double Contract::GetDelta()
{
    return _delta;
}

//! getter
double Contract::GetGamma()
{
    return _gamma;
}

//! getter
double Contract::GetVega()
{
    return _vega;
}

//! getter
double Contract::GetRho()
{
    return _rho;
}

//! getter
double Contract::GetTheta()
{
    return _theta;
}

//! getter
double Contract::GetVegaBS()
{
    return _vega_bs;
}

//! getter
double Contract::GetVolgaBS()
{
    return _volga_bs;
}

//! relative factor
double Contract::RelativeFactor()
{
    double s = _underlying->GetSpot();
    double fx = 1;
    if ( _underlying->GetCurrency() != _premium_currency )
    {
        fx = _underlying->GetCorrelation()->GetFxSpot( _underlying->GetCurrency()->GetName(),
                                                       _premium_currency->GetName() );
    }
    return 100 / ( s * fx );
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
    string node_name = _underlying->GetName() + "#spot";
    node_name += ( udl_ccy != ctr_ccy ) ? "#quanto_" + ctr_ccy : "";

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

SingleSet Contract::GetSingleSet()
{
    return _underlying->GetSingleSet();
}