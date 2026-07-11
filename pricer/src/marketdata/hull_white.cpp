#include "thoth.hpp"
#include "hull_white.hpp"
#include "object_reader.hpp"

//! hull_white.cpp — Hull-White 1F parameters and the closed-form identities the
//! engines consume (see the header for the model and conventions).

HullWhite::HullWhite( const string& ObjectName ) : MarketData( ObjectName, KIND_HULL_WHITE )
{
}

HullWhite::~HullWhite() = default;

//! read the two parameters. The mean reversion is absolute (0.03 = 3%/y pullback)
//! and must be strictly positive (the a -> 0 limits are not implemented);
//! the volatility follows the engine-wide percent convention (1.0 -> 0.01).
void HullWhite::Configure( ObjectReader& reader )
{
    _mean_reversion = reader.Get<double>( "mean_reversion" );
    _volatility = reader.Get<double>( "volatility" ) / 100;
    if ( _mean_reversion <= 0 )
    {
        ERR( "hull_white '" + _name + "' : mean_reversion must be strictly positive" );
    }
    if ( _volatility < 0 )
    {
        ERR( "hull_white '" + _name + "' : volatility must be non-negative" );
    }
}

double HullWhite::A() const
{
    return _mean_reversion;
}

double HullWhite::Sigma() const
{
    return _volatility;
}

//! B(t) = (1 - e^{-a t}) / a
double HullWhite::B( double t ) const
{
    return ( 1 - exp( -_mean_reversion * t ) ) / _mean_reversion;
}

//! V(t) = Var( int_0^t x du ): with x an OU factor from 0,
//!   V(t) = (sigma/a)^2 ( t - 2 B(t) + (1 - e^{-2 a t}) / (2 a) ).
//! This is also sigma^2 * int_0^t B(u)^2 du with B time-to-t (same closed form).
double HullWhite::VarIntegral( double t ) const
{
    const double a = _mean_reversion;
    const double c = ( 1 - exp( -2 * a * t ) ) / ( 2 * a );
    return _volatility * _volatility / ( a * a ) * ( t - 2 * B( t ) + c );
}

//! rate add-on to the total BS variance of a T-maturity option (T-forward-measure
//! variance of S/P(t,T) minus the pure equity part):
//!   2 rho sigma_S sigma_r int_0^T B(T-t) dt + sigma_r^2 int_0^T B(T-t)^2 dt
//! with int B = (T - B(T))/a and sigma_r^2 int B^2 = VarIntegral(T).
double HullWhite::EffectiveVarianceAddOn( double SigmaS, double Rho, double T ) const
{
    const double int_b = ( T - B( T ) ) / _mean_reversion;
    return 2 * Rho * SigmaS * _volatility * int_b + VarIntegral( T );
}
