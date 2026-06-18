#include "thoth.hpp"
#include "variance_swap.hpp"

//! constructor
VarianceSwap::VarianceSwap( const string& ObjectName ) : Contract( ObjectName, KIND_VARIANCE_SWAP )
{
}

VarianceSwap::~VarianceSwap() = default;

//! setters
void VarianceSwap::SetMaturityDate( const date& MaturityDate )
{
    _maturity_date = MaturityDate;
}

void VarianceSwap::SetVolatilityStrike( double VolatilityStrike )
{
    _volatility_strike = VolatilityStrike;
}

void VarianceSwap::SetNotional( double Notional )
{
    _notional = Notional;
}

//! getter
date VarianceSwap::GetMaturityDate() const
{
    return _maturity_date;
}

//! analytic fair value by static replication (Demeterfi-Derman-Kamal-Zou 1999).
//! With the strike boundary set at the forward F, the fair variance is the
//! 1/K^2-weighted strip of out-of-the-money options:
//!   K_fair = (2/T) e^{rT} [ integral_0^F P(K)/K^2 dK + integral_F^inf C(K)/K^2 dK ]
//! and PV = notional * DF * (K_fair - K_var). This is model-free in the vols: it
//! integrates the implied surface, so a smile (SABR) feeds in; for a flat vol it
//! reproduces sigma^2.
//! static-replication strike strip (Demeterfi-Derman-Kamal-Zou): how many ATM
//! standard deviations the OTM-option grid spans around the forward, and how many
//! trapezoid points discretise it.
static constexpr double VARSWAP_STRIP_SIGMA_SPAN = 6.0;
static constexpr int VARSWAP_STRIP_POINTS = 800;

void VarianceSwap::ANA_EvalPrice()
{
    double t = YearFraction( _today, _maturity_date );
    double df = _premium_currency->GetRate()->GetDiscountFactor( _maturity_date );
    double fwd = _underlying->GetForward( _maturity_date, _premium_currency );

    //! strike grid : +/- span ATM std devs around the forward, trapezoidal rule
    double sigma_atm = _underlying->GetImplicitVol( fwd, _maturity_date );
    double span = VARSWAP_STRIP_SIGMA_SPAN * sigma_atm * sqrt( t );
    double k_lo = fwd * exp( -span );
    double k_hi = fwd * exp( span );
    const int n = VARSWAP_STRIP_POINTS;
    double dk = ( k_hi - k_lo ) / n;

    double integral = 0;
    for ( int i = 0; i <= n; i++ )
    {
        double k = k_lo + i * dk;
        if ( k <= 0 )
        {
            continue;
        }
        double vol = _underlying->GetImplicitVol( k, _maturity_date );
        //! out-of-the-money leg: puts below the forward, calls above (prices are
        //! discounted, so e^{rT} = 1/df is applied to the whole integral below)
        double price = ( k < fwd ) ? BS_Put_Price( fwd, k, t, vol, df )
                                   : BS_Call_Price( fwd, k, t, vol, df );
        double weight = ( i == 0 || i == n ) ? 0.5 : 1.0; //!< trapezoid endpoints
        integral += weight * price / ( k * k ) * dk;
    }

    double k_fair = ( t > 0 ) ? ( 2.0 / ( t * df ) ) * integral : 0;
    double k_var = _volatility_strike * _volatility_strike;

    _valuation.premium = _notional * df * ( k_fair - k_var );
    _valuation.vega_bs = _notional * df * 2.0 * sqrt( ( k_fair > 0 ) ? k_fair : 0.0 ); //!< dPV/dvol
    _valuation.delta = 0;
    _valuation.gamma = 0;
}

//! closed form for equity-like underlyings (flat or smile-ATM vol surface)
bool VarianceSwap::ANA_HasSolution()
{
    return _underlying->IsGriddable();
}

//! priced on the spot grid via the expected-accumulated-variance PDE (the pricer
//! routes a variance swap to SolveVarianceGrid). Same underlyings as the analytic.
bool VarianceSwap::PDE_HasSolution()
{
    return _underlying->IsGriddable();
}

//! the variance PDE has a zero terminal condition and its own (remaining-variance)
//! boundaries — there is no terminal spot payoff here, so this is just 0.
double VarianceSwap::Intrinsic( const double /*Spot*/ )
{
    return 0.0;
}

bool VarianceSwap::IsAmerican()
{
    return false;
}

//! Monte-Carlo flow: realized variance of the simulated path vs the strike
MonteCarloNode* VarianceSwap::GetFlowNode( NodeCollector& NC,
                                           const date& /*AsOfDate*/ )
{
    return NC.GetOrCreate<VarianceSwapFlowNode>( _name + node_name::FLOW,
                                                 [&]( VarianceSwapFlowNode* V )
                                                 {
                                                     V->SetSpotNode( GetUnderlyingNode( NC ) );
                                                     V->SetStrikeVariance( _volatility_strike * _volatility_strike );
                                                     V->SetNotional( _notional );
                                                     V->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );
                                                 } );
}

//! single payment at maturity
set<date> VarianceSwap::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

set<date> VarianceSwap::GetFlowDates()
{
    return GetFixingDates();
}

set<date> VarianceSwap::GetAmericanExerciseDates()
{
    return GetFixingDates();
}
