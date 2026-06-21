#include "thoth.hpp"
#include "volatility.hpp"
#include "object_reader.hpp"

//!
Volatility::Volatility( const string& ObjectName,
                        const string& ObjectKind ) : MarketData( ObjectName, ObjectKind )
{
    _non_working_days_weight = NON_WORKING_DAYS_WEIGHT;
}

//!
Volatility::~Volatility() = default;

//! optional calendar shared by every volatility: the field holds the name of a
//! calendar object whose non_working_days_weight scales the vol (a second reader
//! bound to that object reads its field).
void Volatility::ConfigureCommon( ObjectReader& reader )
{
    if ( reader.Has<string>( "calendar" ) )
    {
        ObjectReader calendar( reader.Manager(), reader.Get<string>( "calendar" ) );
        SetNonWorkingDaysWeight( calendar.Get<double>( "non_working_days_weight" ) );
    }
}

//!
double Volatility::GetLocalVolatility( const double Strike,
                                       const date& MaturityDate,
                                       const double Spot,
                                       const double RiskFreeRate,
                                       const double ContinuousDividend )
{

    //! faster naming conventions
    double S = Spot;
    double K = Strike;
    double r = RiskFreeRate;
    double q = ContinuousDividend;
    date t = MaturityDate;
    double T = YearFraction( _today, t );

    //! finite-difference bumps: dK in strike units, dT in years (must match the
    //! ±1 calendar-day shift used for the time bumps below)
    const double dK = .01;
    const double dT = 1.0 / NB_OF_DAYS_A_YEAR;

    //! forward to each (possibly time-bumped) maturity: F(tau) = S * exp((r-q) tau)
    double F = S * exp( ( r - q ) * T );
    double F_Td = S * exp( ( r - q ) * YearFraction( _today, t - days( 1 ) ) );
    double F_Tu = S * exp( ( r - q ) * YearFraction( _today, t + days( 1 ) ) );

    // mid vol
    double V = GetImplicitVol( K, F, t );

    // derivatives
    double V_Kd = GetImplicitVol( K - dK, F, t );
    double V_Ku = GetImplicitVol( K + dK, F, t );
    double V_Td = GetImplicitVol( K, F_Td, t - days( 1 ) );
    double V_Tu = GetImplicitVol( K, F_Tu, t + days( 1 ) );

    // calculus
    double sT = sqrt( T );
    double V2 = V * V;
    double d1 = ( log( S / K ) + ( r - q + .5 * V2 ) * T ) / V / sT;
    double dVdK = ( V_Ku - V_Kd ) / ( 2 * dK );
    double dVdK2 = ( V_Ku + V_Kd - 2 * V ) / ( dK * dK );
    double dVdT = ( V_Tu - V_Td ) / ( 2 * dT );
    double N = 2 * V * T * ( dVdT + ( r - q ) * K * dVdK ) + V2;
    double D1 = 1 + K * d1 * dVdK * sT;
    double D = D1 * D1 + K * K * T * V * ( dVdK2 - d1 * dVdK * dVdK * sT );

    //! Dupire local variance. The implied surface is not guaranteed arbitrage-free
    //! everywhere (Hagan's SABR expansion develops a small calendar/butterfly
    //! arbitrage in the wings at long maturities), so N/D can turn slightly
    //! negative there and sqrt would yield a NaN that poisons every path. Floor
    //! the variance at a tiny positive value: it only bites in those degenerate
    //! wing nodes (near-ATM paths are unaffected, so the Dupire repricing still
    //! matches the analytic price) and keeps the diffusion real and finite.
    const double local_var = N / D;
    const double floor = 1e-8; //!< (0.01% vol)^2
    return sqrt( local_var > floor ? local_var : floor );
}

//! setter
void Volatility::SetNonWorkingDaysWeight( double Weight )
{
    _non_working_days_weight = Weight;
}

double Volatility::GetDayWeight()
{
    return sqrt( ( 5. + 2. * _non_working_days_weight ) / 7. );
}
