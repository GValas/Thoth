#include "thoth.hpp"
#include "absolute_basket.hpp"

//!
AbsoluteBasket::AbsoluteBasket( const string& ObjectName ) : Basket( ObjectName, KIND_BASKET )
{
}

//!
AbsoluteBasket::~AbsoluteBasket() = default;

//! setter
void AbsoluteBasket::SetWeightList( gsl_vector* WeightList )
{
    _weight_list = WeightList;
}

//!
double AbsoluteBasket::GetSpot()
{
    return 100 * ext_gsl_vector_sum( _weight_list );
}

//! compute equivalent BS vol
double AbsoluteBasket::GetImplicitVol( const double Strike,
                                       const date& MaturityDate )

{

    // mkt data
    size_t n = _underlying_list.size();
    gsl_vector* fwds = gsl_vector_alloc( n );
    gsl_vector* vols = gsl_vector_alloc( n );
    vector<string> udl_list;
    vector<Forex*> fx_list;
    double dt = YearFraction( _today, MaturityDate );
    for ( size_t i = 0; i < n; i++ )
    {
        Underlying* u = _underlying_list[i];
        gsl_vector_set( fwds, i, u->GetForward( MaturityDate, _currency ) / u->GetSpot() * _weight_list->data[i] );
        gsl_vector_set( vols, i, u->GetImplicitVol( Strike, MaturityDate ) * sqrt( dt ) );
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

    //! moment matching
    double M1, M2, M3, M4;
    gsl_matrix* correls = _correlation->ExtractMatrix( udl_list, fx_list );
    LN_to_M4( fwds, vols, correls, M1, M2, M3, M4 );

    //! shifted log normal formula
    double Mu, Var, D;
    M3_to_SLN( M1, M2, M3, Mu, Var, D );
    double df = _currency->GetRate()->GetDiscountFactor( MaturityDate );
    double premium = 100 * SLN_Call_Price( M1, Strike / 100, df, Mu, Var, D );

    //! price -> vol
    double fwd = GetForward( MaturityDate, _currency );
    double vol = BS_Call_ImplicitVol( fwd, Strike, dt, premium, df );

    //! free memory
    gsl_vector_free( fwds );
    gsl_vector_free( vols );
    gsl_matrix_free( correls );

    return vol;
}

//!
double AbsoluteBasket::GetForward( const date& MaturityDate,
                                   Currency* QuantoCurrency )
{
    double f = 0;
    size_t n = _underlying_list.size();
    for ( size_t i = 0; i < n; i++ )
    {
        Underlying* u = _underlying_list[i];
        f += u->GetForward( MaturityDate, QuantoCurrency ) / u->GetSpot() * gsl_vector_get( _weight_list, i );
    }
    return 100 * f;
}

MonteCarloNode* AbsoluteBasket::GetNode( NodeCollector& NC )

{
    //! sum-product node over the basket underlyings
    return NC.GetOrCreate<AbsoluteBasketNode>(
        _name + "#spot",
        [&]( AbsoluteBasketNode* E )
        {
            for ( size_t i = 0; i < _underlying_list.size(); i++ )
            {
                E->PushUnderlying( _underlying_list[i]->GetNode( NC ) );
                E->PushWeight( 100 * gsl_vector_get( _weight_list, i ) / _underlying_list[i]->GetSpot() );
            }
        } );
}
//! mcl node
MonteCarloNode* AbsoluteBasket::GetCorrelNode( NodeCollector& /*NC*/,
                                               const string& /*UnderlyingCurrency*/,
                                               const string& /*BaseCurrency*/ )
{
    //! quanto correlation node for a relative basket is not implemented
    ERR( "basket '" + _name + "' : GetCorrelNode not implemented" );
}

//! mcl node
MonteCarloNode* AbsoluteBasket::GetVolNode( NodeCollector& /*NC*/ )
{
    //! composite vol node for a relative basket is not implemented
    ERR( "basket '" + _name + "' : GetVolNode not implemented" );
}
