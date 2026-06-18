#include "thoth.hpp"
#include "composite.hpp"

//!
Composite::Composite( const string& ObjectName ) : Underlying( ObjectName, KIND_COMPOSITE )
{
    _underlying = nullptr;
}

//!
Composite::~Composite() = default;

//! setter
void Composite::SetCompoCurrency( Currency& CompositeCurrency )
{
    _currency = &CompositeCurrency;
}

//! setter
void Composite::SetUnderlying( Underlying& Underlying )
{
    _underlying = &Underlying;
}

//! getter
Underlying* Composite::GetUnderlying() const
{
    return _underlying;
}

//! getter
Currency* Composite::GetCompositeCurrency() const
{
    return _currency;
}

//!
double Composite::GetSpot() const
{
    double eq_spot = _underlying->GetSpot();
    double fx_spot = _correlation->GetFxSpot( _underlying->GetCurrency()->GetName(),
                                              _currency->GetName() );
    return eq_spot * fx_spot;
}

//! pricing compo = modifying vol
double Composite::GetImplicitVol( const double Strike,
                                  const date& MaturityDate )
{
    double v_eq = _underlying->GetImplicitVol( Strike, MaturityDate );
    double v_fx = _correlation->GetFxVol( _underlying->GetCurrency()->GetName(),
                                          _currency->GetName() );
    double rho = _correlation->GetValue( _underlying->GetCurrency()->GetName(),
                                         _currency->GetName(),
                                         _underlying->Object::GetName() );
    double v = sqrt( v_eq * v_eq + v_fx * v_fx + 2 * rho * v_eq * v_fx );
    return v;
}

//!
//! pricing quanto = correcting drift
double Composite::GetForward( const date& MaturityDate,
                              Currency* QuantoCurrency )
{

    double df = _currency->GetRate()->GetDiscountFactor( MaturityDate );
    double dt = YearFraction( _today, MaturityDate );

    //! quanto adjustment
    double qto = 1;
    if ( QuantoCurrency != _currency )
    {
        double v_eq = _underlying->GetImplicitVol( 0, MaturityDate );
        double v_fx = _correlation->GetFxVol( _currency->GetName(), QuantoCurrency->GetName() );
        double rho = -_correlation->GetValue( _currency->GetName(),
                                              QuantoCurrency->GetName(),
                                              _underlying->GetName() );
        qto = exp( ( rho * v_fx * v_eq + v_fx * v_fx ) * dt );
    }

    return GetSpot() * qto / df;
}

//! list of singles
SingleSet Composite::GetSingleSet() const
{
    SingleSet s1 = _underlying->GetSingleSet();
    ForexSet s2 = _correlation->GetForexSet( _underlying->GetCurrency()->GetName(), _currency->GetName() );
    SingleSet s;
    s.insert( s1.begin(), s1.end() );
    s.insert( s2.begin(), s2.end() );
    return s;
}

//! list of singles
CurrencySet Composite::GetCurrencySet() const
{
    CurrencySet s;
    s.insert( _currency );
    s.insert( _underlying->GetCurrency() );
    return s;
}

//! setter
void Composite::SetCorrelation( Correlation* Correlation )
{
    _underlying->SetCorrelation( Correlation );
    Underlying::SetCorrelation( Correlation );
}

//! mcl node
MonteCarloNode* Composite::GetCorrelNode( NodeCollector& NC,
                                          const string& UnderlyingCurrency,
                                          const string& BaseCurrency )
{
    string node_name = _underlying->GetName() + "_compo_" + _currency->GetName() +
                       "#" + UnderlyingCurrency + "_" + BaseCurrency + "#correl";
    return NC.GetOrCreate<CompositeCorrelNode>(
        node_name,
        [&]( CompositeCorrelNode* C )
        {
            C->SetRhoSABNode( _correlation->GetCorrelNode( NC,
                                                           UnderlyingCurrency,
                                                           BaseCurrency,
                                                           _underlying->GetName() ) );
            C->SetRhoIJABNode( _correlation->GetCorrelNode( NC,
                                                            _underlying->GetCurrency()->GetName(),
                                                            _currency->GetName(),
                                                            UnderlyingCurrency,
                                                            BaseCurrency ) );
            C->SetVolSNode( _underlying->GetVolNode( NC ) );
            C->SetVolSIJNode( GetVolNode( NC ) );
            C->SetVolIJNode( _correlation->GetFxVolNode( NC,
                                                         _underlying->GetCurrency()->GetName(),
                                                         _currency->GetName() ) );
        } );
}

//! mcl node
MonteCarloNode* Composite::GetVolNode( NodeCollector& NC )
{
    string node_name = _underlying->GetName() + "_compo_" + _currency->GetName() + "#vol";
    return NC.GetOrCreate<CompositeVolNode>(
        node_name,
        [&]( CompositeVolNode* V )
        {
            V->SetRhoSXNode( _correlation->GetCorrelNode( NC,
                                                          _underlying->GetCurrency()->GetName(),
                                                          _currency->GetName(),
                                                          _underlying->GetName() ) );
            V->SetVolSNode( _underlying->GetVolNode( NC ) );
            V->SetVolXNode( _correlation->GetFxVolNode( NC,
                                                        _underlying->GetCurrency()->GetName(),
                                                        _currency->GetName() ) );
        } );
}

//! mcl node
MonteCarloNode* Composite::GetNode( NodeCollector& NC )
{
    //! node already exists ?
    MonteCarloNode* N;
    string eq = _underlying->GetName();
    string eq_ccy = _underlying->GetCurrency()->GetName();
    string cpo_ccy = _currency->GetName();
    string node_name = eq + "_compo_" + cpo_ccy + "#spot";

    if ( !( N = NC.GetNode( node_name ) ) )
    {

        ProductNode* E = NC.NewNode<ProductNode>( node_name );

        // quanto adjusted mono
        MonteCarloNode* M;
        string mono_name = eq + "#spot#quanto_" + cpo_ccy;
        if ( !( M = NC.GetNode( mono_name ) ) )
        {
            M = _underlying->GetNode( NC );
            QuantoAdjustmentNode* Q = NC.NewNode<QuantoAdjustmentNode>( mono_name );
            Q->SetUdlSpotNode( M );
            Q->SetUdlFxCorrelNode( _underlying->GetCorrelNode( NC, eq_ccy, cpo_ccy ) );
            Q->SetUdlVolNode( _underlying->GetVolNode( NC ) );
            Q->SetFxVolNode( _correlation->GetFxVolNode( NC, eq_ccy, cpo_ccy ) );
            M = Q;
        }

        //! fx
        MonteCarloNode* X = _correlation->GetFxNode( NC,
                                                     _underlying->GetCurrency()->GetName(),
                                                     _currency->GetName() );
        E->PushNode( M, 1 );
        E->PushNode( X, 1 );
        N = E;
    }

    return N;
}