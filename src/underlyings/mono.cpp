#include "thoth.hpp"
#include "mono.hpp"

Mono::Mono( const string& ObjectName,
            const string& ObjectKind ) : Underlying( ObjectName, ObjectKind )
{
}

Mono::~Mono() = default;

////! list of singles
// set<string> Mono::GetSingleNameList()
//{
//     set<string> s;
//     s.insert( _name );
//     return s;
// }
//
////! list of currencies
// set<string> Mono::GetCurrencyNameList()
//{
//     set<string> s;
//     s.insert( _currency->GetName() );
//     return s;
// }

//! list of singles
SingleSet Mono::GetSingleSet() const
{
    SingleSet s;
    s.insert( _single );
    return s;
}

void Mono::SetSingle( Single& Single )
{
    _single = &Single;
    _currency = _single->GetCurrency();
}

double Mono::GetSpot() const
{
    return _single->GetSpot();
}

double Mono::GetDiffusionSpot( const date& LastDate ) const
{
    return _single->GetDiffusionSpot( LastDate );
}

CurrencySet Mono::GetCurrencySet() const
{
    CurrencySet s;
    s.insert( _single->GetCurrency() );
    return s;
}

double Mono::GetForward( const date& MaturityDate,
                         Currency* QuantoCurrency )
{
    double f = _single->GetForward( MaturityDate );

    //! quanto drift correction: when the payoff is settled in a currency other
    //! than the underlying's, the asset's forward in the payoff-currency measure
    //! is F *= exp( -rho(S,FX) * sigma_S * sigma_X * t ). This mirrors the MCL
    //! QuantoAdjustmentNode (exp(-v*w*c*dt) on the spot) so ANA/PDE/MCL agree.
    //! rho is the signed FX-underlying correlation (GetValue already carries the
    //! sign), so no extra negation here. The correction uses the ATM implied vol
    //! (GetImplicitVol at strike 0); under a flat BS surface this is exact, but
    //! for a local/stochastic-vol surface it is an approximation of the
    //! instantaneous-vol drift term. Currency identity is by pointer: all
    //! Currency objects are singletons in the book graph, so this is exact.
    if ( QuantoCurrency && QuantoCurrency != _currency )
    {
        if ( !_correlation )
        {
            ERR( "missing correlation for quanto adjustment of " + _name );
        }
        double dt = YearFraction( _today, MaturityDate );
        double v_s = _single->GetImplicitVol( 0, MaturityDate );
        double v_fx = _correlation->GetFxVol( _currency->GetName(), QuantoCurrency->GetName() );
        double rho = _correlation->GetValue( _currency->GetName(), QuantoCurrency->GetName(), _name );
        f *= exp( -rho * v_s * v_fx * dt );
    }
    return f;
}

double Mono::GetImplicitVol( const double Strike,
                             const date& MaturityDate )
{
    return _single->GetImplicitVol( Strike, MaturityDate );
}

MonteCarloNode* Mono::GetNode( NodeCollector& NC )
{
    return _single->GetNode( NC );
}

MonteCarloNode* Mono::GetVolNode( NodeCollector& NC )
{
    return _single->GetVolNode( NC );
}

MonteCarloNode* Mono::GetCorrelNode( NodeCollector& NC,
                                     const string& UnderlyingCurrency,
                                     const string& BaseCurrency )
{
    return _correlation->GetCorrelNode( NC, UnderlyingCurrency, BaseCurrency, _name );
}