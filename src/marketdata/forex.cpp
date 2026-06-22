#include "thoth.hpp"
#include "forex.hpp"
#include "object_reader.hpp"

//! forex.cpp — Forex implementation: currency-pair wiring, flat vol, FX drift node.

//! constructor — tag as an FX kind; the underlying currency is wired in Configure
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

//! setter — bind the underlying (foreign) currency by address
void Forex::SetUnderlyingCurrency( Currency& UnderlyingCurrency )
{
    _underlying_currency = &UnderlyingCurrency;
}

//! setter — the base (domestic) currency is the Asset's pricing currency, so route it
//! through Asset::SetCurrency rather than a separate field
void Forex::SetBaseCurrency( Currency& BaseCurrency )
{
    SetCurrency( BaseCurrency );
}

//! getter — base (domestic) currency, i.e. the Asset's pricing currency
Currency* Forex::GetBaseCurrency() const
{
    return GetCurrency();
}

//! getter — underlying (foreign) currency
Currency* Forex::GetUnderlyingCurrency() const
{

    return _underlying_currency;
}

//! getter — the flat FX volatility level
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

//! local vol — FX vol is flat, so the surface is strike/maturity independent and this
//! reduces to the constant vol level
double Forex::GetLocalVolatility( const double /*Strike*/,
                                  const date& /*MaturityDate*/ )
{
    return GetConstantVol();
}

//! FX drift node — the diffusion drift is the domestic-minus-foreign rate differential
//! (covered interest parity: a forward FX rate grows at r_dom - r_for), so wire both
//! currencies' term-structured rate nodes into the DriftNode
MonteCarloNode* Forex::GetDriftNode( NodeCollector& NC )
{
    return NC.GetOrCreate<DriftNode>(
        _name + node_name::DRIFT,
        [&]( DriftNode* D )
        {
            D->SetDomesticRateNode( _currency->GetRateNode( NC ) );           //!< base/domestic leg
            D->SetForeignRateNode( _underlying_currency->GetRateNode( NC ) ); //!< underlying/foreign leg
        } );
}