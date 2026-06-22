#include "thoth.hpp"
#include "variance_swap.hpp"
#include "object_reader.hpp"

//! Variance-swap implementation: configuration, the model-free static-replication
//! fair-variance (Demeterfi-Derman-Kamal-Zou) closed form, and the Monte-Carlo flow
//! node that compares realized to strike variance.

//! constructor
VarianceSwap::VarianceSwap( const string& ObjectName ) : Contract( ObjectName, KIND_VARIANCE_SWAP )
{
}

VarianceSwap::~VarianceSwap() = default;

//! read own fields, then the common contract attributes
void VarianceSwap::Configure( ObjectReader& reader )
{
    SetMaturityDate( reader.Get<date>( "maturity" ) );
    //! volatility_strike is in percent (like every vol), stored as decimal
    SetVolatilityStrike( reader.Get<double>( "volatility_strike" ) / 100.0 );
    SetNotional( reader.Get<double>( "notional", 1 ) );
    ConfigureCommon( reader );
}

//! setters
//! setter — maturity / single settlement date
void VarianceSwap::SetMaturityDate( const date& MaturityDate )
{
    _maturity_date = MaturityDate;
}

//! setter — strike as a volatility (decimal); squared to a variance strike at use
void VarianceSwap::SetVolatilityStrike( double VolatilityStrike )
{
    _volatility_strike = VolatilityStrike;
}

//! setter — variance notional (PV scales linearly in it)
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
    //! year fraction, discount factor and the drift-carrying forward at maturity
    double t = YearFraction( _today, _maturity_date );
    double df = _premium_currency->GetRate()->GetDiscountFactor( _maturity_date );
    double fwd = _underlying->GetForward( _maturity_date, _premium_currency );

    //! build the strike grid (+/- span ATM std devs around the forward) and sample
    //! the implied-vol surface at each strike, then integrate the strip in finance.cpp.
    //! The grid is uniform in LOG-strike (geometric spacing): the 1/K^2 replication
    //! weight and the lognormal density both decay geometrically, so a log grid puts
    //! its resolution where the integrand has mass (near the forward) instead of
    //! wasting points in the far upper wing a linear-K grid over-samples — tighter
    //! integration for the same point count, and strikes stay strictly positive.
    //! half-width of the log-strike strip = span ATM standard deviations to maturity;
    //! dlogk is the uniform log-strike step over 2*span (geometric strike spacing)
    double sigma_atm = _underlying->GetImplicitVol( fwd, _maturity_date );
    double span = VARSWAP_STRIP_SIGMA_SPAN * sigma_atm * sqrt( t );
    double dlogk = 2.0 * span / VARSWAP_STRIP_POINTS;

    vector<double> strikes;
    vector<double> vols;
    strikes.reserve( VARSWAP_STRIP_POINTS + 1 );
    vols.reserve( VARSWAP_STRIP_POINTS + 1 );
    for ( int i = 0; i <= VARSWAP_STRIP_POINTS; i++ )
    {
        //! k = F * e^{-span + i*dlogk} sweeps geometrically from F e^{-span} to F e^{+span}
        double k = fwd * exp( -span + i * dlogk );
        strikes.push_back( k );
        vols.push_back( _underlying->GetImplicitVol( k, _maturity_date ) );
    }

    //! integrate the 1/K^2-weighted OTM strip into the fair variance, then assemble
    //! PV = notional * DF * (fair_var - strike_var); vega is reported w.r.t. vol
    double k_fair = VarSwap_FairVariance( fwd, t, df, strikes, vols );
    double k_var = _volatility_strike * _volatility_strike; //!< strike vol -> strike variance

    _valuation.premium = VarSwap_Price( _notional, df, k_fair, k_var );
    _valuation.vega_bs = VarSwap_Vega( _notional, df, k_fair );
    _valuation.delta = 0; //!< first-order spot-neutral by construction (model-free strip)
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

//! variance swaps settle only at maturity: no early exercise
bool VarianceSwap::IsAmerican()
{
    return false;
}

//! Monte-Carlo flow: realized variance of the simulated path vs the strike.
//! Build (or fetch) a VarianceSwapFlowNode wired to the spot node, the strike
//! variance (vol^2) and the notional, settling at maturity.
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

//! single payment at maturity (the realized variance is accumulated by the flow
//! node over the diffusion grid; only the settlement date is exposed here)
set<date> VarianceSwap::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! same single date as the fixing
set<date> VarianceSwap::GetFlowDates()
{
    return GetFixingDates();
}

//! no early exercise: same single date
set<date> VarianceSwap::GetAmericanExerciseDates()
{
    return GetFixingDates();
}
