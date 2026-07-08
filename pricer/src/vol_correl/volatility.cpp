//! volatility.cpp — abstract volatility-surface base.
//!
//! Holds the calendar day-weight shared by every concrete surface (bs / sabr /
//! heston) and provides the model-independent Dupire local-volatility transform
//! (GetLocalVolatility): given an implied surface it builds the local vol the PDE
//! diffusion must use, by finite-differencing the implied surface in strike and
//! time. Concrete surfaces only supply GetImplicitVol; the Dupire algebra lives
//! here so every kind reuses it.

#include "thoth.hpp"
#include "volatility.hpp"
#include "object_reader.hpp"

//! upper cap on the Dupire local vol, as a multiple of the node's implied vol:
//! backstop against the D -> 0+ wing degeneracy of Hagan's expansion (see
//! GetLocalVolatility). 5x the implied vol is far above any healthy local/implied
//! ratio, so pricing is unchanged outside the degenerate nodes.
static constexpr double LOCAL_VOL_CAP_FACTOR = 5.0;

//! constructor: seed the day-weight with the project default (overridden later if
//! the YAML names a calendar with its own non_working_days_weight)
Volatility::Volatility( const string& ObjectName,
                        const string& ObjectKind ) : MarketData( ObjectName, ObjectKind )
{
    _non_working_days_weight = NON_WORKING_DAYS_WEIGHT;
}

//!
Volatility::~Volatility() = default;

//! optional calendar shared by every volatility: the field holds the name of a
//! calendar object whose non_working_days_weight scales the vol (a second reader
//! bound to that object reads its field). Each concrete Configure calls this base first.
void Volatility::Configure( ObjectReader& reader )
{
    if ( reader.Has<string>( "calendar" ) )
    {
        ObjectReader calendar( reader.Manager(), reader.Get<string>( "calendar" ) );
        _non_working_days_weight = calendar.Get<double>( "non_working_days_weight" );
    }
}

//! Dupire local volatility at (K, T) derived from the implied surface.
//!
//! Implements Dupire's formula in its implied-vol (rather than call-price) form:
//! given the implied-vol surface V(K,T) it returns the instantaneous local vol
//! sigma_loc(K,T) that reproduces the same European prices in a 1-factor PDE.
//! All the V-derivatives below are computed by central finite differences on
//! GetImplicitVol, so this works for any concrete surface (notably SABR).
//!
//! @param Strike, MaturityDate   the (K, T) grid node the PDE asks for
//! @param Spot, RiskFreeRate, ContinuousDividend   diffusion inputs; the forward
//!        F(tau) = S e^{(r-q)tau} enters the moneyness and the time-derivative term
//! @return sigma_loc = sqrt(local variance), floored to stay real (see below)
double Volatility::GetLocalVolatility( const double Strike,
                                       const date& MaturityDate,
                                       const double Spot,
                                       const double RiskFreeRate,
                                       const double ContinuousDividend )
{

    //! faster naming conventions (match the textbook Dupire notation)
    double S = Spot;
    double K = Strike;
    double r = RiskFreeRate;
    double q = ContinuousDividend;
    date t = MaturityDate;
    double T = YearFraction( _today, t ); //!< time to maturity in years

    //! finite-difference bumps: dK in strike units, dT in years (must match the
    //! ±1 calendar-day shift used for the time bumps below)
    const double dK = .01;
    const double dT = 1.0 / NB_OF_DAYS_A_YEAR;

    //! forward to each (possibly time-bumped) maturity: F(tau) = S * exp((r-q) tau)
    double F = S * exp( ( r - q ) * T );
    double F_Td = S * exp( ( r - q ) * YearFraction( _today, t - days( 1 ) ) );
    double F_Tu = S * exp( ( r - q ) * YearFraction( _today, t + days( 1 ) ) );

    // mid (central) implied vol at the node
    double V = GetImplicitVol( K, F, t );

    // surrounding samples for the central differences: ±dK in strike, ±1 day in
    // time (each time sample uses the forward to *its own* bumped maturity)
    double V_Kd = GetImplicitVol( K - dK, F, t );
    double V_Ku = GetImplicitVol( K + dK, F, t );
    double V_Td = GetImplicitVol( K, F_Td, t - days( 1 ) );
    double V_Tu = GetImplicitVol( K, F_Tu, t + days( 1 ) );

    // assemble Dupire's implied-vol formula
    //   sigma_loc^2 = N / D, with
    //   N = sigma^2 + 2 sigma T (dsigma/dT + (r-q) K dsigma/dK)
    //   D = (1 + K d1 sqrt(T) dsigma/dK)^2
    //       + K^2 T sigma (d2sigma/dK2 - d1 (dsigma/dK)^2 sqrt(T))
    double sT = sqrt( T );
    double V2 = V * V;
    double d1 = ( log( S / K ) + ( r - q + .5 * V2 ) * T ) / V / sT;        //!< Black d1 at the node
    double dVdK = ( V_Ku - V_Kd ) / ( 2 * dK );                             //!< first strike slope
    double dVdK2 = ( V_Ku + V_Kd - 2 * V ) / ( dK * dK );                   //!< strike convexity (skew curvature)
    double dVdT = ( V_Tu - V_Td ) / ( 2 * dT );                             //!< calendar slope
    double N = 2 * V * T * ( dVdT + ( r - q ) * K * dVdK ) + V2;            //!< numerator (calendar + drift terms)
    double D1 = 1 + K * d1 * dVdK * sT;                                     //!< inner bracket of the skew term
    double D = D1 * D1 + K * K * T * V * ( dVdK2 - d1 * dVdK * dVdK * sT ); //!< denominator (skew + convexity)

    //! Dupire local variance. The implied surface is not guaranteed arbitrage-free
    //! everywhere (Hagan's SABR expansion develops a small calendar/butterfly
    //! arbitrage in the wings at long maturities), so N/D can turn slightly
    //! negative there and sqrt would yield a NaN that poisons every path. Floor
    //! the variance at a tiny positive value: it only bites in those degenerate
    //! wing nodes (near-ATM paths are unaffected, so the Dupire repricing still
    //! matches the analytic price) and keeps the diffusion real and finite.
    //!
    //! The same wing arbitrage can also degenerate the OTHER way: at extreme
    //! nu*sqrt(T) the denominator D collapses towards 0+ and N/D blows up to a
    //! spuriously HUGE local variance that a floor alone lets through, distorting
    //! the diffusion (PDE assembly and MC grids both read this). Cap the local vol
    //! at LOCAL_VOL_CAP_FACTOR times the node's own implied vol V: in a healthy
    //! surface the local/implied ratio stays far below that, so the cap only bites
    //! in the same degenerate wing nodes the floor targets.
    const double local_var = N / D;
    const double floor = 1e-8; //!< (0.01% vol)^2
    const double cap = LOCAL_VOL_CAP_FACTOR * V * ( LOCAL_VOL_CAP_FACTOR * V );
    if ( local_var > cap )
    {
        return LOCAL_VOL_CAP_FACTOR * V;
    }
    return sqrt( local_var > floor ? local_var : floor );
}

//! calendar day-weight applied to the vol level. Variance accrues on trading days
//! at full rate and on the 2 weekend days at _non_working_days_weight; over a
//! 7-day week the average variance fraction is (5 + 2*w)/7, so the vol multiplier
//! is its square root. Weight = 1 makes this 1 (no calendar effect).
double Volatility::GetDayWeight()
{
    return sqrt( ( 5. + 2. * _non_working_days_weight ) / 7. );
}
