#include "thoth.hpp"
#include "equity.hpp"

//! constructor
Equity::Equity( const string& ObjectName ) : Single( ObjectName, KIND_EQUITY )
{
    _continuous_dividends = nullptr;
    _repo = nullptr;
}

Equity::~Equity() = default;

//! setter
void Equity::SetRepo( RepoCurve* Repo )
{
    _repo = Repo;
}

//! setter
void Equity::SetContinuousDividends( ContinuousDividendsCurve* ContinuousDividends )
{
    _continuous_dividends = ContinuousDividends;
}

//! getter
RepoCurve* Equity::GetRepo()
{
    return _repo;
}

//! getter
ContinuousDividendsCurve* Equity::GetContinuousDividends()
{
    return _continuous_dividends;
}

//! getter
double Equity::GetSpot()
{
    return Single::GetSpot();
}

//!
bool Equity::UseLocalVol()
{
    return _volatility->_is_local;
}

//!
double Equity::GetForward( const date& MaturityDate )
{
    double dt = YearFraction( Object::_today, MaturityDate );
    double df = Asset::_currency->GetRate()->GetDiscountFactor( MaturityDate );
    double div = ( _continuous_dividends ? _continuous_dividends->GetCurveValue( MaturityDate ) : 0 );

    //! quanto adjustment
    double qto = 0;
    /*
    if ( Asset::_currency != QuantoCurrency )
    {
        //! correlation must be set
        if ( !_correlation )
        {
            ERR( "missing correlation for quanto adjustment computation of " + Object::_name );
        }

        double v = GetImplicitVol( 0, MaturityDate );
        double v_fx = _correlation->GetFxVol( Asset::_currency->GetName(), QuantoCurrency->GetName() );
        double rho  = _correlation->GetValue( Asset::_currency->GetName(), QuantoCurrency->GetName(), Object::_name );
        qto = v_fx * v * rho;
    }
    */

    return _spot * exp( -dt * ( div + qto ) ) / df;
}

//! implicit vol
double Equity::GetImplicitVol( const double Strike,
                               const date& MaturityDate )
{
    return Single::GetImplicitVol( Strike, MaturityDate );
}

//! local vol
double Equity::GetLocalVolatility( const double Strike,
                                   const date& MaturityDate )
{
    double r = _currency->GetRate()->GetCurveValue( MaturityDate );
    //! repo and continuous dividends are independently optional; sum whichever is present
    double q = 0;
    if ( _repo )
    {
        q += _repo->GetCurveValue( MaturityDate );
    }
    if ( _continuous_dividends )
    {
        q += _continuous_dividends->GetCurveValue( MaturityDate );
    }
    return _volatility->GetLocalVolatility( Strike, MaturityDate, _spot, r, q );
}

MonteCarloNode* Equity::GetNode( NodeCollector& NC )
{
    return Single::GetNode( NC );
}

MonteCarloNode* Equity::GetDriftNode( NodeCollector& NC )
{

    return NC.GetOrCreate<DriftNode>(
        _name + "#drift",
        [&]( DriftNode* D )
        {
            D->SetDomesticRateNode( Asset::_currency->GetRateNode( NC ) );
            D->SetDividendRateNode( ( _continuous_dividends ) ? _continuous_dividends->GetNode( NC ) : nullptr );
            D->SetRepoRateNode( ( _repo ) ? _repo->GetNode( NC ) : nullptr );
        } );
}