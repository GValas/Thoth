#include "finance.hpp"

//! vanilla price
double payoff_vanilla( const double spot,
                       const double strike,
                       const OptionType type,
                       const bool has_cap,
                       const double cap,
                       const bool has_floor,
                       const double floor )
{
    //! vanilla part
    double vanilla = ( type == OptionType::Call ) ? ( spot - strike ) : ( strike - spot );

    //! cap/floor
    if ( has_cap )
    {
        vanilla = min( vanilla, cap );
    }
    if ( has_floor )
    {
        vanilla = max( vanilla, floor );
    }

    return vanilla;
}

double payoff_digital( const double spot,
                       const string& barrier_type,
                       const double barrier_up_level,
                       const double barrier_down_level )
{
    double digital;
    if ( barrier_type == BARRIER_TYPE_UP_AND_OUT )
    {
        digital = ( spot >= barrier_up_level ) ? 0 : 1;
    }
    else if ( barrier_type == BARRIER_TYPE_UP_AND_IN )
    {
        digital = ( spot >= barrier_up_level ) ? 1 : 0;
    }
    else if ( barrier_type == BARRIER_TYPE_DOWN_AND_OUT )
    {
        digital = ( spot <= barrier_down_level ) ? 0 : 1;
    }
    else if ( barrier_type == BARRIER_TYPE_DOWN_AND_IN )
    {
        digital = ( spot <= barrier_down_level ) ? 1 : 0;
    }
    else
    {
        ERR( "ERR> unknown barrier type '" + barrier_type + "'" );
    }
    return digital;
}

//! bs call price
double BS_Call_Price( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return DiscountFactor * max( Forward - Strike, 0.0 );
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double d2 = d1 - v_sqr_t;
        double Nd1 = gsl_cdf_ugaussian_P( d1 );
        double Nd2 = gsl_cdf_ugaussian_P( d2 );
        return DiscountFactor * ( Forward * Nd1 - Strike * Nd2 );
    }
}

//! bs put price, call/put parity
double BS_Put_Price( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor )
{
    double c = BS_Call_Price( Forward, Strike, TimeToMaturity, Volatility, DiscountFactor );
    double p = c - DiscountFactor * ( Forward - Strike );
    return p;
}

//! bs call delta
double BS_Call_Delta( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return ( Forward > Strike ? DiscountFactor : 0 );
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double Nd1 = gsl_cdf_ugaussian_P( d1 );
        return DiscountFactor * Nd1;
    }
}

//! bs put delta, call/put parity
double BS_Put_Delta( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor )
{
    double c = BS_Call_Delta( Forward, Strike, TimeToMaturity, Volatility, DiscountFactor );
    double p = c - 1;
    return p;
}

//! bs vega
double BS_Vega( const double Forward,
                const double Strike,
                const double TimeToMaturity,
                const double Volatility,
                const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return 0;
    }
    else
    {
        double sqr_t = sqrt( TimeToMaturity );
        double v_sqr_t = Volatility * sqr_t;
        double d2 = log( Forward / Strike ) / v_sqr_t - 0.5 * v_sqr_t;
        double Fd2 = gsl_ran_ugaussian_pdf( d2 );
        return DiscountFactor * Strike * sqr_t * Fd2;
    }
}

//! bs gamma
double BS_Gamma( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return 0;
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double Fd1 = gsl_ran_ugaussian_pdf( d1 );
        return DiscountFactor * Fd1 / Forward / v_sqr_t;
    }
}

//! bs volga
double BS_Volga( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return 0;
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double d2 = d1 - v_sqr_t;
        double Fd1 = gsl_ran_ugaussian_pdf( d1 );
        return DiscountFactor * Forward * Fd1 * v_sqr_t * d1 * d2;
    }
}

// implicit vol
double BS_Call_ImplicitVol( const double Forward,
                            const double Strike,
                            const double TimeToMaturity,
                            const double Price,
                            const double DiscountFactor )
{

    //! vol start point
    double vega, vol_error, vol = INITIAL_IMPLICIT_VOL;
    int i = 0;
    do
    {
        if ( !( vega = BS_Vega( Forward, Strike, TimeToMaturity, vol, DiscountFactor ) ) )
        {
            //! zero vega: Newton cannot proceed — fail loudly rather than returning a -1 "vol"
            ERR( "BS_Call_ImplicitVol: zero vega, cannot invert price " + ToString( Price ) );
        }
        else
        {
            vol_error = Price - BS_Call_Price( Forward, Strike, TimeToMaturity, vol, DiscountFactor );
            vol += vol_error / vega;
        }
    } while ( abs( vol_error ) > IMPLICIT_VOL_MAX_ERROR && ++i < IMPLICIT_VOL_MAX_ITERATIONS );

    return vol;
}