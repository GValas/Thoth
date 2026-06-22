#include "thoth.hpp"
#include "composite.hpp"
#include "correlation.hpp"
#include "currency.hpp"
#include "object_reader.hpp"

//! composite.cpp — composite/quanto pricing and node wiring.
//!
//! All cross-asset data (FX spot/vol, equity-FX correlation) is read off the shared
//! Correlation object: it doubles as the FX market and the correlation matrix. The
//! composite vol formula and the quanto drift below are the closed-form analogues
//! of what the QuantoAdjustmentNode / CompositeVolNode reproduce path-wise.

//! ctor — KIND_COMPOSITE; null the wrapped underlying until Configure/SetUnderlying.
Composite::Composite( const string& ObjectName ) : Underlying( ObjectName, KIND_COMPOSITE )
{
    _underlying = nullptr;
}

//! wrapped underlying is non-owning; default destruction.
Composite::~Composite() = default;

//! read the wrapped underlying (field "equity") and the composite currency.
//! "equity" is the foreign-quoted asset; "composite_currency" is the settlement ccy.
void Composite::Configure( ObjectReader& reader )
{
    SetUnderlying( *reader.Ref<Underlying>( "equity" ) );
    SetCompoCurrency( *reader.Ref<Currency>( "composite_currency" ) );
}

//! setter — the composite/settlement currency is kept in the base _currency slot.
void Composite::SetCompoCurrency( Currency& CompositeCurrency )
{
    _currency = &CompositeCurrency;
}

//! setter — the wrapped foreign-currency underlying.
void Composite::SetUnderlying( Underlying& Underlying )
{
    _underlying = &Underlying;
}

//! getter — the wrapped underlying.
Underlying* Composite::GetUnderlying() const
{
    return _underlying;
}

//! getter — the composite/settlement currency.
Currency* Composite::GetCompositeCurrency() const
{
    return _currency;
}

//! spot in the settlement currency: convert the asset spot at the live FX rate
//! quote->settlement. FX spot comes from the Correlation/FX market by ccy names.
double Composite::GetSpot() const
{
    double eq_spot = _underlying->GetSpot();
    double fx_spot = _correlation->GetFxSpot( _underlying->GetCurrency()->GetName(),
                                              _currency->GetName() );
    return eq_spot * fx_spot;
}

//! pricing compo = modifying vol.
//! The composite payoff is the product S_eq*FX of two lognormals, whose log-variance
//! is the sum of the two log-variances plus twice their covariance, hence
//!   v_compo = sqrt(v_eq^2 + v_fx^2 + 2*rho*v_eq*v_fx).
//! rho is the equity / (quote->settlement) FX correlation looked up by name.
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
//! pricing quanto = correcting drift.
//! When the payoff settles in a QuantoCurrency different from this composite's own
//! currency, the asset is converted at a *fixed* FX, so under the settlement measure
//! its drift picks up the quanto term rho*sigma_fx*sigma_eq (and a sigma_fx^2 convexity
//! term). The forward is then the FX-driftless spot grown by exp(that term)/df.
double Composite::GetForward( const date& MaturityDate,
                              Currency* QuantoCurrency )
{

    double df = _currency->GetRate()->GetDiscountFactor( MaturityDate );
    double dt = YearFraction( _today, MaturityDate );

    //! quanto adjustment — only when the settlement (quanto) ccy differs; otherwise
    //! qto stays 1 and the forward is the plain GetSpot()/df.
    double qto = 1;
    if ( QuantoCurrency != _currency )
    {
        double v_eq = _underlying->GetImplicitVol( 0, MaturityDate ); //!< ATM vol (strike 0 => representative)
        double v_fx = _correlation->GetFxVol( _currency->GetName(), QuantoCurrency->GetName() );
        //! sign flip: correlation is stored for ccy->quanto, but the drift uses the
        //! correlation of the asset with the *inverse* FX move, hence the leading minus.
        double rho = -_correlation->GetValue( _currency->GetName(),
                                              QuantoCurrency->GetName(),
                                              _underlying->GetName() );
        //! quanto drift over [today,T]: exp((rho*sigma_fx*sigma_eq + sigma_fx^2)*dt).
        qto = exp( ( rho * v_fx * v_eq + v_fx * v_fx ) * dt );
    }

    return GetSpot() * qto / df;
}

//! single-name set: the wrapped underlying's factors plus the FX leg(s) needed to
//! convert quote->settlement (the Forex objects are themselves single-name drivers).
SingleSet Composite::GetSingleSet() const
{
    SingleSet s1 = _underlying->GetSingleSet();
    ForexSet s2 = _correlation->GetForexSet( _underlying->GetCurrency()->GetName(), _currency->GetName() );
    SingleSet s;
    s.insert( s1.begin(), s1.end() );
    s.insert( s2.begin(), s2.end() );
    return s;
}

//! currency set: the settlement currency and the wrapped underlying's quote currency
//! (both rate curves are needed — discounting in settlement, drift in quote).
CurrencySet Composite::GetCurrencySet() const
{
    CurrencySet s;
    s.insert( _currency );
    s.insert( _underlying->GetCurrency() );
    return s;
}

//! setter — propagate correlation into the wrapped underlying first (it issues its
//! own quanto/FX queries), then record it on this composite via the base.
void Composite::SetCorrelation( Correlation* Correlation )
{
    _underlying->SetCorrelation( Correlation );
    Underlying::SetCorrelation( Correlation );
}

//! mcl node — composite *correlation* node feeding a higher-level quanto adjustment.
//! It assembles the inputs needed to express the correlation between the composite
//! driver S*FX(quote->settlement) and a target FX pair (UnderlyingCurrency/BaseCurrency):
//! the two pairwise correlations (asset vs target FX, quote->settlement FX vs target FX)
//! and the three vols (asset, composite, settlement-FX). The CompositeCorrelNode then
//! combines them path-wise into the effective correlation.
MonteCarloNode* Composite::GetCorrelNode( NodeCollector& NC,
                                          const string& UnderlyingCurrency,
                                          const string& BaseCurrency )
{
    //! cache key encodes both the composite identity and the target FX pair so two
    //! different quanto targets don't collide on one node.
    string node_name = _underlying->GetName() + "_compo_" + _currency->GetName() +
                       "#" + UnderlyingCurrency + "_" + BaseCurrency + node_name::CORREL;
    return NC.GetOrCreate<CompositeCorrelNode>(
        node_name,
        [&]( CompositeCorrelNode* C )
        {
            //! rho(asset, target-FX) — correlation of the underlying with the target pair
            C->SetRhoSABNode( _correlation->GetCorrelNode( NC,
                                                           UnderlyingCurrency,
                                                           BaseCurrency,
                                                           _underlying->GetName() ) );
            //! rho(quote->settlement FX, target-FX) — the two FX pairs' correlation
            C->SetRhoIJABNode( _correlation->GetCorrelNode( NC,
                                                            _underlying->GetCurrency()->GetName(),
                                                            _currency->GetName(),
                                                            UnderlyingCurrency,
                                                            BaseCurrency ) );
            C->SetVolSNode( _underlying->GetVolNode( NC ) ); //!< sigma_asset
            C->SetVolSIJNode( GetVolNode( NC ) );            //!< sigma_composite (S*FX)
            C->SetVolIJNode( _correlation->GetFxVolNode( NC, //!< sigma_settlement-FX
                                                         _underlying->GetCurrency()->GetName(),
                                                         _currency->GetName() ) );
        } );
}

//! mcl node — composite *vol* node implementing
//! sigma_compo = sqrt(sigma_S^2 + sigma_X^2 + 2*rho_SX*sigma_S*sigma_X) path-wise
//! (the stochastic-vol analogue of GetImplicitVol). Inputs: the asset/FX correlation
//! and the two component vol nodes.
MonteCarloNode* Composite::GetVolNode( NodeCollector& NC )
{
    string node_name = _underlying->GetName() + "_compo_" + _currency->GetName() + node_name::VOL;
    return NC.GetOrCreate<CompositeVolNode>(
        node_name,
        [&]( CompositeVolNode* V )
        {
            V->SetRhoSXNode( _correlation->GetCorrelNode( NC, //!< rho(asset, quote->settlement FX)
                                                          _underlying->GetCurrency()->GetName(),
                                                          _currency->GetName(),
                                                          _underlying->GetName() ) );
            V->SetVolSNode( _underlying->GetVolNode( NC ) ); //!< sigma_asset
            V->SetVolXNode( _correlation->GetFxVolNode( NC,  //!< sigma_FX
                                                        _underlying->GetCurrency()->GetName(),
                                                        _currency->GetName() ) );
        } );
}

//! mcl node — composite *spot* node: S_eq*FX as a ProductNode of (a) the quanto-drift
//! adjusted asset spot and (b) the quote->settlement FX spot. Hand-built (rather than
//! GetOrCreate) because it nests an inner cached QuantoAdjustmentNode keyed separately.
MonteCarloNode* Composite::GetNode( NodeCollector& NC )
{
    //! node already exists ? (manual cache check — the product node and its inner
    //! quanto node have distinct cache keys, so each is de-duplicated independently)
    MonteCarloNode* N;
    string eq = _underlying->GetName();
    string eq_ccy = _underlying->GetCurrency()->GetName();
    string cpo_ccy = _currency->GetName();
    string node_name = eq + "_compo_" + cpo_ccy + node_name::SPOT;

    if ( !( N = NC.GetNode( node_name ) ) )
    {

        ProductNode* E = NC.NewNode<ProductNode>( node_name );

        // quanto adjusted mono — the asset spot with its drift corrected for the
        // quote->settlement quanto effect (so the product below is consistent with
        // the closed-form quanto forward). Cached under its own key for reuse.
        MonteCarloNode* M;
        string mono_name = eq + "#spot#quanto_" + cpo_ccy;
        if ( !( M = NC.GetNode( mono_name ) ) )
        {
            M = _underlying->GetNode( NC ); //!< raw (un-adjusted) asset spot node
            QuantoAdjustmentNode* Q = NC.NewNode<QuantoAdjustmentNode>( mono_name );
            Q->SetUdlSpotNode( M );                                                     //!< raw spot
            Q->SetUdlFxCorrelNode( _underlying->GetCorrelNode( NC, eq_ccy, cpo_ccy ) ); //!< rho(asset,FX)
            Q->SetUdlVolNode( _underlying->GetVolNode( NC ) );                          //!< sigma_asset
            Q->SetFxVolNode( _correlation->GetFxVolNode( NC, eq_ccy, cpo_ccy ) );       //!< sigma_FX
            M = Q;                                                                      //!< from here on M is the quanto-adjusted spot
        }

        //! fx — the quote->settlement conversion factor node
        MonteCarloNode* X = _correlation->GetFxNode( NC,
                                                     _underlying->GetCurrency()->GetName(),
                                                     _currency->GetName() );
        E->PushNode( M, 1 ); //!< exponent 1: linear factor (adjusted asset spot)
        E->PushNode( X, 1 ); //!< exponent 1: linear factor (FX spot) => product = S*FX
        N = E;
    }

    return N;
}