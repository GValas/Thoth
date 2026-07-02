#include "thoth.hpp"
#include "finance.hpp"
#include "maths.hpp"
#include "absolute_basket.hpp"
#include "correlation.hpp"
#include "object_reader.hpp"

//! absolute_basket.cpp — weighted-sum basket pricing.
//!
//! Two consistent views of the same B = 100 * sum_i w_i * S_i / S_i0 index:
//!  - Monte-Carlo: GetNode builds an AbsoluteBasketNode that sums the rebased,
//!    weighted per-component spot nodes path by path.
//!  - Closed-form / PDE: the sum of lognormal component forwards is approximated by
//!    a single shifted-lognormal (SLN) via 4-moment matching, priced with the SLN
//!    call formula, then inverted to a BS-equivalent vol the 1-D engines can use.

//! ctor — tag as KIND_BASKET so the registry/engine dispatch recognises the shape.
AbsoluteBasket::AbsoluteBasket( const string& ObjectName ) : Basket( ObjectName, KIND_BASKET )
{
}

//! nothing owned beyond the base; default destruction.
AbsoluteBasket::~AbsoluteBasket() = default;

//! read the component underlyings and weights, then fix the rebasing reference.
//! Order matters: the list must be set before CaptureReferenceSpots so S_i0 is
//! sampled from the freshly resolved components.
void AbsoluteBasket::Configure( ObjectReader& reader )
{
    SetUnderlyingList( reader.Ref<vector<Underlying>>( "underlyings" ) );
    SetWeightList( reader.LaVector( "weights" ) );
    CaptureReferenceSpots(); //!< fix the rebasing reference (S_i0) at load
}

//! setter — store the weight vector (aliases the reader-owned la_vector).
void AbsoluteBasket::SetWeightList( la_vector* WeightList )
{
    _weight_list = WeightList;
}

//! spot at t0: each component's rebased performance S_i/S_i0 == 1, so the index is
//! 100 * sum_i w_i. (Same expression GetForward collapses to when MaturityDate==today.)
double AbsoluteBasket::GetSpot() const
{
    return 100 * ext_la_vector_sum( _weight_list );
}

//! compute equivalent BS vol.
//!
//! Method: the basket index is a weighted sum of lognormal component forwards. We
//! match its first four moments to a shifted lognormal (SLN), price an SLN call at
//! the requested strike, then back out the single BS vol that reproduces that price
//! at the basket forward. That BS-equivalent vol is what the 1-D ANA/PDE engines
//! consume in place of a true (non-existent) basket vol.
double AbsoluteBasket::GetImplicitVol( const double Strike,
                                       const date& MaturityDate )

{

    //! a strike <= 0 is the "representative vol" query (e.g. the PDE grid setup):
    //! invert at the ATM basket forward instead, since a strike-0 call has no vega
    double k = ( Strike > 0 ) ? Strike : GetForward( MaturityDate, _currency );

    // mkt data (RAII: freed even if a callee below throws via ERR)
    //! build the per-component forward and total-vol vectors that feed the moment
    //! matcher. fwds[i] is the *weighted, rebased* forward (the contribution of
    //! component i to the index); vols[i] is the component's total vol sigma*sqrt(t).
    size_t n = _underlying_list.size();
    LaVector fwds = la_vector_alloc( n );
    LaVector vols = la_vector_alloc( n );
    vector<string> udl_list;
    vector<Forex*> fx_list;
    double dt = YearFraction( _today, MaturityDate );
    for ( size_t i = 0; i < n; i++ )
    {
        Underlying* u = _underlying_list[i];
        //! weighted, rebased forward: F_i / S_i0 * w_i  (the 100-scale is applied to
        //! M1/premium below). RefSpot(i) is the FIXED inception spot S_i0.
        la_vector_set( fwds, i, u->GetForward( MaturityDate, _currency ) / RefSpot( i ) * _weight_list->data[i] );
        //! integrated vol over [today, T] — moment matching works on total, not
        //! instantaneous, vol because the moments are of the terminal distribution.
        la_vector_set( vols, i, u->GetImplicitVol( k, MaturityDate ) * sqrt( dt ) );
        udl_list.push_back( u->GetName() );
    }

    /*
    double Alpha, Beta;
    M2_to_IG(M1, M2, Alpha, Beta);
    premium = IG_Call_Price(M1, strike, df, Alpha, Beta);

    double Mu, Var;
    M2_to_LN(M1, M2, Mu, Var);
    premium = LN_Call_Price(M1, strike, df, Mu, Var);
    */

    //! moment matching: pull the component cross-correlation sub-matrix (ordered to
    //! match udl_list), then compute the first four central moments M1..M4 of the
    //! weighted lognormal sum. ExtractMatrix needs the same name order as fwds/vols.
    double M1, M2, M3, M4;
    LaMatrix correls = _correlation->ExtractMatrix( udl_list, fx_list );
    LN_to_M4( fwds, vols, correls, M1, M2, M3, M4 );

    //! shifted log normal formula: fit an SLN (mean Mu, var Var, shift D) to the
    //! first three moments, then price the SLN call. k/100 and the *100 scale undo
    //! the index's 100-base so M1 and the strike live on the same scale.
    double Mu, Var, D;
    M3_to_SLN( M1, M2, M3, Mu, Var, D );
    double df = _currency->GetRate()->GetDiscountFactor( MaturityDate );
    double premium = 100 * SLN_Call_Price( M1, k / 100, df, Mu, Var, D );

    //! price -> vol: invert the (lognormal) BS call at the basket forward to recover
    //! the single equivalent vol that reproduces the moment-matched SLN premium.
    double fwd = GetForward( MaturityDate, _currency );
    double vol = BS_Call_ImplicitVol( fwd, k, dt, premium, df );

    return vol;
}

//! basket forward in QuantoCurrency: the weighted sum of the components' rebased
//! forwards, 100-scaled — 100 * sum_i w_i * F_i / S_i0. This is exactly the mean of
//! the index distribution moment-matched above, keeping ANA/PDE/MCL consistent.
double AbsoluteBasket::GetForward( const date& MaturityDate,
                                   Currency* QuantoCurrency )
{
    double f = 0;
    size_t n = _underlying_list.size();
    for ( size_t i = 0; i < n; i++ )
    {
        Underlying* u = _underlying_list[i];
        f += u->GetForward( MaturityDate, QuantoCurrency ) / RefSpot( i ) * la_vector_get( _weight_list, i );
    }
    return 100 * f;
}

//! basket spot node — a sum-product node that, each path, forms the rebased weighted
//! sum of the member spot nodes. The per-component coefficient folds the 100-scale,
//! the weight and the fixed 1/S_i0 rebasing into one constant (100*w_i/S_i0), so the
//! node just dots its child spots with these weights.
MonteCarloNode* AbsoluteBasket::GetNode( NodeCollector& NC )

{
    //! sum-product node over the basket underlyings
    return NC.GetOrCreate<AbsoluteBasketNode>(
        _name + node_name::SPOT,
        [&]( AbsoluteBasketNode* E )
        {
            for ( size_t i = 0; i < _underlying_list.size(); i++ )
            {
                //! child = component spot node; coefficient = 100 * w_i / S_i0 so the
                //! node yields 100 * sum_i w_i * S_i / S_i0 directly.
                E->PushUnderlying( _underlying_list[i]->GetNode( NC ) );
                E->PushWeight( 100 * la_vector_get( _weight_list, i ) / RefSpot( i ) );
            }
        } );
}
//! mcl node — unsupported for a (relative) weighted basket: it has no single driver,
//! hence no quanto correlation node. Fails loudly rather than returning a wrong node.
MonteCarloNode* AbsoluteBasket::GetCorrelNode( NodeCollector& /*NC*/,
                                               const string& /*UnderlyingCurrency*/,
                                               const string& /*BaseCurrency*/ )
{
    //! quanto correlation node for a relative basket is not implemented
    ERR( "basket '" + _name + "' : GetCorrelNode not implemented" );
}

//! mcl node — unsupported: a weighted basket has no single composite vol node
//! (its terminal law is the moment-matched SLN, not a tradable instantaneous vol).
MonteCarloNode* AbsoluteBasket::GetVolNode( NodeCollector& /*NC*/ )
{
    //! composite vol node for a relative basket is not implemented
    ERR( "basket '" + _name + "' : GetVolNode not implemented" );
}
