#include "thoth.hpp"
#include "sabr_volatility.hpp"

//!
SabrVolatility::SabrVolatility( const string& ObjectName ) : Volatility( ObjectName, KIND_SABR_VOLATILITY )
{
    _is_local = true;
}

//!
SabrVolatility::~SabrVolatility() = default;

void SabrVolatility::SetSpot( double Spot ) { _spot = Spot; }
void SabrVolatility::SetMaturityList( const vector<double>& MaturityList ) { _maturity_list = MaturityList; }
void SabrVolatility::SetAlphaList( const vector<double>& AlphaList ) { _alpha_list = AlphaList; }
void SabrVolatility::SetBetaList( const vector<double>& BetaList ) { _beta_list = BetaList; }
void SabrVolatility::SetRhoList( const vector<double>& RhoList ) { _rho_list = RhoList; }
void SabrVolatility::SetNuList( const vector<double>& NuList ) { _nu_list = NuList; }

//! linear interpolation in time, flat beyond the first/last maturity
double SabrVolatility::Interp( const vector<double>& Values, double T ) const
{
    if ( T <= _maturity_list.front() )
    {
        return Values.front();
    }
    if ( T >= _maturity_list.back() )
    {
        return Values.back();
    }
    for ( size_t i = 1; i < _maturity_list.size(); i++ )
    {
        if ( T <= _maturity_list[i] )
        {
            double w = ( T - _maturity_list[i - 1] ) / ( _maturity_list[i] - _maturity_list[i - 1] );
            return Values[i - 1] + w * ( Values[i] - Values[i - 1] );
        }
    }
    return Values.back();
}

//! Hagan (2002) lognormal SABR implied volatility
double SabrVolatility::GetImplicitVol( const double Strike,
                                       const date& MaturityDate )
{
    double T = YearFraction( _today, MaturityDate );
    if ( T <= 0 )
    {
        T = 1.0 / NB_OF_DAYS_A_YEAR; //!< guard for same-day maturity bumps
    }
    double F = _spot;
    //! the engine passes Strike = 0 as a sentinel for the reference (ATM) vol
    double K = ( Strike > 0 ) ? Strike : F;

    double alpha = Interp( _alpha_list, T );
    double beta = Interp( _beta_list, T );
    double rho = Interp( _rho_list, T );
    double nu = Interp( _nu_list, T );

    double one_mb = 1.0 - beta;
    double log_fk = log( F / K );
    double fk_pow = pow( F * K, one_mb / 2.0 );

    //! denominator series in log(F/K)
    double den = fk_pow * ( 1.0 + one_mb * one_mb / 24.0 * log_fk * log_fk +
                            one_mb * one_mb * one_mb * one_mb / 1920.0 * log_fk * log_fk * log_fk * log_fk );

    //! z / x(z) -> 1 as z -> 0 (ATM limit)
    double z = ( nu / alpha ) * fk_pow * log_fk;
    double zxz = 1.0;
    if ( fabs( z ) > 1e-12 )
    {
        double xz = log( ( sqrt( 1.0 - 2.0 * rho * z + z * z ) + z - rho ) / ( 1.0 - rho ) );
        zxz = z / xz;
    }

    //! time-correction factor
    double corr = 1.0 + ( one_mb * one_mb / 24.0 * alpha * alpha / pow( F * K, one_mb ) +
                          0.25 * rho * beta * nu * alpha / fk_pow +
                          ( 2.0 - 3.0 * rho * rho ) / 24.0 * nu * nu ) *
                            T;

    //! + vega bump shift (parallel shift of the implied surface), if any
    return ( alpha / den ) * zxz * corr + _vol_shift;
}

//! SABR is a local-vol surface for the PDE pricer : no Monte-Carlo node
MonteCarloNode* SabrVolatility::GetNode( NodeCollector& /*NC*/ )
{
    ERR( "sabr_volatility '" + _name + "' : Monte-Carlo node not supported (use the PDE pricer)" );
}
