#include "thoth.hpp"
#include "forex.hpp"
#include "object_reader.hpp"

Forex::Forex( const string& ObjectName ) : Single( ObjectName, KIND_FOREX )
{
    _underlying_currency = nullptr;
}

Forex::~Forex() = default;

//! read the currency pair, with an optional volatility and spot
void Forex::Configure( ObjectReader& reader )
{
    SetBaseCurrency( *reader.Ref<Currency>( "base_currency" ) );
    SetUnderlyingCurrency( *reader.Ref<Currency>( "underlying_currency" ) );
    if ( reader.Has<string>( "volatility" ) )
    {
        SetVolatility( *reader.Ref<Volatility>( "volatility" ) );
    }
    if ( reader.Has<double>( "spot" ) )
    {
        SetSpot( reader.Get<double>( "spot" ) );
    }
}

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
Currency* Forex::GetBaseCurrency() const
{
    return GetCurrency();
}

//! getter
Currency* Forex::GetUnderlyingCurrency() const
{

    return _underlying_currency;
}

//! getter
double Forex::GetConstantVol() const
{
    //! FX vol is flat (bs_volatility), so the forward is ignored; pass 0
    return _volatility->GetImplicitVol( 0, 0, date( 0 ) );
}

double Forex::GetForward( const date& /*MaturityDate*/ ) const
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
        _name + node_name::DRIFT,
        [&]( DriftNode* D )
        {
            D->SetDomesticRateNode( _currency->GetRateNode( NC ) );
            D->SetForeignRateNode( _underlying_currency->GetRateNode( NC ) );
        } );
}