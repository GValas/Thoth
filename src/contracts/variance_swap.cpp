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
//! trapezoid points discretise it. The grid is built here and handed to
//! VarSwap_FairVariance (finance.cpp), which integrates the strip.
static constexpr double VARSWAP_STRIP_SIGMA_SPAN = 6.0;
static constexpr int VARSWAP_STRIP_POINTS = 800;

void VarianceSwap::ANA_EvalPrice()
{
    double t = YearFraction( _today, _maturity_date );
    double df = _premium_currency->GetRate()->GetDiscountFactor( _maturity_date );
    double fwd = _underlying->GetForward( _maturity_date, _premium_currency );

    //! build the strike grid (+/- span ATM std devs around the forward) and sample
    //! the implied-vol surface at each strike, then integrate the strip in finance.cpp
    double sigma_atm = _underlying->GetImplicitVol( fwd, _maturity_date );
    double span = VARSWAP_STRIP_SIGMA_SPAN * sigma_atm * sqrt( t );
    double k_lo = fwd * exp( -span );
    double k_hi = fwd * exp( span );
    double dk = ( k_hi - k_lo ) / VARSWAP_STRIP_POINTS;

    vector<double> strikes;
    vector<double> vols;
    strikes.reserve( VARSWAP_STRIP_POINTS + 1 );
    vols.reserve( VARSWAP_STRIP_POINTS + 1 );
    for ( int i = 0; i <= VARSWAP_STRIP_POINTS; i++ )
    {
        double k = k_lo + i * dk;
        if ( k <= 0 )
        {
            continue;
        }
        strikes.push_back( k );
        vols.push_back( _underlying->GetImplicitVol( k, _maturity_date ) );
    }

    double k_fair = VarSwap_FairVariance( fwd, t, df, strikes, vols );
    double k_var = _volatility_strike * _volatility_strike;

    _valuation.premium = VarSwap_Price( _notional, df, k_fair, k_var );
    _valuation.vega_bs = VarSwap_Vega( _notional, df, k_fair );
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
