#include "thoth.hpp"
#include "forex.hpp"

Forex::Forex( const string& ObjectName ) : Single( ObjectName, KIND_FOREX )
{
    _underlying_currency = nullptr;
}

Forex::~Forex() = default;

//! setter
void Forex::SetUnderlyingCurrency( Currency& UnderlyingCurrency )
{
    _underlying_currency = &UnderlyingCurrency;
}

//! setter
void Forex::SetBaseCurrency( Currency& BaseCurrency )
{
    SetCurrency( BaseCurrency );
}

//! getter
Currency* Forex::GetBaseCurrency()
{
    return GetCurrency();
}

//! getter
Currency* Forex::GetUnderlyingCurrency()
{

    return _underlying_currency;
}

//! getter
double Forex::GetConstantVol() const
{
    return _volatility->GetImplicitVol( 0, date( 0 ) );
}

double Forex::GetForward( const date& /*MaturityDate*/ )
{
    //! FX forward is not implemented; fail loudly rather than returning 0
    ERR( "forex '" + _name + "' : GetForward not implemented" );
}

//! local vol
double Forex::GetLocalVolatility( const double /*Strike*/,
                                         const date& /*MaturityDate*/ )
{
    return GetConstantVol();
}

MonteCarloNode* Forex::GetDriftNode( NodeCollector& NC )
{
    return NC.GetOrCreate<DriftNode>(
        _name + "#drift",
        [&]( DriftNode* D )
        {
            D->SetDomesticRateNode( _currency->GetRateNode( NC ) );
            D->SetForeignRateNode( _underlying_currency->GetRateNode( NC ) );
        } );
}