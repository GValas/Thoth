#include "thoth.hpp"
#include "pricer_ana.hpp"
#include "barrier.hpp"
#include "cancellation.hpp"
#include "enums.hpp"
#include "progress_bar.hpp"
#include "single.hpp" //!< Volatility / StochasticVolParams (MonoVol helper)
#include "vanilla.hpp"
#include "variance_swap.hpp"

//! a closed-form pricer is just a Pricer; no engine state to initialise
PricerANA::PricerANA( const string& ObjectName,
                      YamlConfig& YamlConfig ) : Pricer( ObjectName, YamlConfig, KIND_ANA_PRICER )
{
}

PricerANA::~PricerANA() = default;

namespace
{
//! the single's volatility iff the underlying is a mono (exactly one single name),
//! else null. Used to detect a stochastic-vol (Heston) model and route to the
//! single-asset closed form.
Volatility* MonoVol( Underlying* u )
{
    SingleSet s = u->GetSingleSet();
    return ( s.size() == 1 ) ? ( *s.begin() )->GetVolatility() : nullptr;
}

//! static-replication strike strip (Demeterfi-Derman-Kamal-Zou): how many ATM
//! standard deviations the OTM-option grid spans around the forward, and how many
//! trapezoid points discretise it. The grid is integrated by VarSwap_FairVariance.
constexpr double VARSWAP_STRIP_SIGMA_SPAN = 6.0;
constexpr int VARSWAP_STRIP_POINTS = 800;
} // namespace

//! check that closed-form resolution is allowed for every contract
void PricerANA::PreCheck()
{
    CheckAllowed( []( Contract* c )
                  { return c->ANA_HasSolution(); }, "ANA (closed-form)" );
}

//! price the whole book by closed-form. One progress bar over the contracts;
//! each step prices the contract and, when requested, its bump-and-revalue
//! Greeks (see Pricer::PriceBookByContract).
void PricerANA::PriceBook()
{
    PriceBookByContract( "ANA" );
}

//! single-contract closed-form price hook used by the per-contract loop / Greeks.
//! The contract is a pure description: dispatch on its type and evaluate here.
void PricerANA::PriceContract( Contract* Ctr )
{
    if ( Vanilla* v = dynamic_cast<Vanilla*>( Ctr ) )
    {
        PriceVanilla( v );
    }
    else if ( Barrier* b = dynamic_cast<Barrier*>( Ctr ) )
    {
        PriceBarrier( b );
    }
    else if ( VarianceSwap* s = dynamic_cast<VarianceSwap*>( Ctr ) )
    {
        PriceVarianceSwap( s );
    }
    else
    {
        //! PreCheck (ANA_HasSolution) rejects any contract without a closed form,
        //! so an unhandled type here is an internal inconsistency.
        ERR( "ANA pricing '" + _name + "': contract '" + Ctr->GetName() +
             "' has no closed-form evaluator" );
    }
}

//! Black-Scholes (or Heston under stochastic vol) for a European vanilla.
void PricerANA::PriceVanilla( Vanilla* Opt )
{
    Valuation& out = Result( Opt );
    Underlying* u = Opt->GetUnderlying();
    Currency* ccy = Opt->GetPremiumCurrency();
    const date maturity = Opt->GetMaturityDate();
    const double k = Opt->GetStrike();
    const bool is_call = ( Opt->GetType() == OptionType::Call );

    //! bs inputs: year fraction, discount factor, drift-carrying forward, implied
    //! vol at this strike/maturity
    double t = YearFraction( _today, maturity );
    double df = ccy->GetRate()->GetDiscountFactor( maturity );
    double f = u->GetForward( maturity, ccy );
    double v = u->GetImplicitVol( k, maturity );

    //! Heston stochastic vol : closed-form via the characteristic function. The
    //! forward f already carries the carry/quanto drift; discount with df. Greeks
    //! come from the per-contract bump-and-revalue (this reprices with bumps), so
    //! the analytic BS Greeks below are left zero.
    Volatility* mvol = MonoVol( u );
    if ( mvol && mvol->IsStochastic() )
    {
        const StochasticVolParams h = mvol->StochasticParams();
        out.premium = is_call
                          ? Heston_Call_Price( f, k, t, df, h.v0, h.kappa, h.theta, h.xi, h.rho,
                                               h.jump_intensity, h.jump_mean, h.jump_vol )
                          : Heston_Put_Price( f, k, t, df, h.v0, h.kappa, h.theta, h.xi, h.rho,
                                              h.jump_intensity, h.jump_mean, h.jump_vol );
        out.delta = out.gamma = out.vega_bs = out.volga_bs = 0;
        return;
    }

    //! deterministic-vol Black-Scholes: closed-form premium + delta per option type
    if ( is_call )
    {
        out.premium = BS_Call_Price( f, k, t, v, df );
        out.delta = BS_Call_Delta( f, k, t, v, df );
    }
    else
    {
        out.premium = BS_Put_Price( f, k, t, v, df );
        out.delta = BS_Put_Delta( f, k, t, v, df );
    }

    //! greeks that are sign-independent of call/put (gamma, vega, volga)
    out.gamma = BS_Gamma( f, k, t, v, df );
    out.vega_bs = BS_Vega( f, k, t, v, df );
    out.volga_bs = BS_Volga( f, k, t, v, df );
}

//! Reiner-Rubinstein closed-form barrier price for a single spot value: decode the
//! barrier flavour (call/put, down/up, in/out, active level) from the contract's
//! (public) definition and delegate to the finance-module formula.
double PricerANA::BarrierPrice( Barrier* Bar, double S, double r, double b, double v, double t, double df )
{
    bool is_call = ( Bar->_type == OptionType::Call );
    bool is_down = ( Bar->_barrier_type == BarrierType::DownAndOut ||
                     Bar->_barrier_type == BarrierType::DownAndIn );
    bool is_in = IsKnockIn( Bar->_barrier_type );
    double H = is_down ? Bar->_barrier_down_level : Bar->_barrier_up_level; //!< active barrier
    return Barrier_Price( S, r, b, v, t, df, H, Bar->_strike, is_call, is_down, is_in );
}

//! closed-form barrier pricing (premium + finite-difference spot Greeks).
void PricerANA::PriceBarrier( Barrier* Bar )
{
    Valuation& out = Result( Bar );
    Underlying* u = Bar->GetUnderlying();
    Currency* ccy = Bar->GetPremiumCurrency();
    const date maturity = Bar->GetMaturityDate();

    //! market data (same conventions as the vanilla)
    double t = YearFraction( _today, maturity );
    double df = ccy->GetRate()->GetDiscountFactor( maturity );
    double f = u->GetForward( maturity, ccy );
    double s = u->GetSpot();
    double v = u->GetImplicitVol( Bar->_strike, maturity );

    //! back out the constant r and cost-of-carry b that reproduce the market df and
    //! forward (r from df = e^{-rt}, b from fwd = s e^{bt}); the closed form is
    //! parametrised in (r, b) rather than (df, fwd)
    double r = ( t > 0 ) ? -log( df ) / t : 0.0;
    double b = ( t > 0 && s > 0 ) ? log( f / s ) / t : 0.0;

    //! premium
    out.premium = BarrierPrice( Bar, s, r, b, v, t, df );

    //! delta & gamma by central finite difference on the spot (the closed form has
    //! no clean analytic Greeks across the case table, so bump-and-revalue). The
    //! bump is symmetric (+/- half a bump) so delta is second-order accurate; gamma
    //! divides by the half-bump squared.
    double s_up = s * ( 1 + GREEK_SPOT_BUMP / 2 );
    double s_dw = s * ( 1 - GREEK_SPOT_BUMP / 2 );
    double p_up = BarrierPrice( Bar, s_up, r, b, v, t, df );
    double p_dw = BarrierPrice( Bar, s_dw, r, b, v, t, df );
    out.delta = ( p_up - p_dw ) / ( s * GREEK_SPOT_BUMP );
    out.gamma = ( p_up + p_dw - 2 * out.premium ) /
                ( s * GREEK_SPOT_BUMP / 2 * s * GREEK_SPOT_BUMP / 2 );
}

//! analytic fair value by static replication (Demeterfi-Derman-Kamal-Zou 1999).
//! With the strike boundary at the forward F, the fair variance is the 1/K^2-weighted
//! strip of out-of-the-money options:
//!   K_fair = (2/T) e^{rT} [ integral_0^F P(K)/K^2 dK + integral_F^inf C(K)/K^2 dK ]
//! and PV = notional * DF * (K_fair - K_var). Model-free in the vols: it integrates
//! the implied surface, so a smile (SABR) feeds in; for a flat vol it reproduces sigma^2.
void PricerANA::PriceVarianceSwap( VarianceSwap* Swap )
{
    Valuation& out = Result( Swap );
    Underlying* u = Swap->GetUnderlying();
    Currency* ccy = Swap->GetPremiumCurrency();
    const date maturity = Swap->GetMaturityDate();

    //! year fraction, discount factor and the drift-carrying forward at maturity
    double t = YearFraction( _today, maturity );
    double df = ccy->GetRate()->GetDiscountFactor( maturity );
    double fwd = u->GetForward( maturity, ccy );

    //! build the strike grid (+/- span ATM std devs around the forward) and sample
    //! the implied-vol surface at each strike, then integrate the strip in finance.cpp.
    //! The grid is uniform in LOG-strike (geometric spacing): the 1/K^2 replication
    //! weight and the lognormal density both decay geometrically, so a log grid puts
    //! its resolution where the integrand has mass (near the forward) instead of
    //! wasting points in the far upper wing a linear-K grid over-samples — tighter
    //! integration for the same point count, and strikes stay strictly positive.
    double sigma_atm = u->GetImplicitVol( fwd, maturity );
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
        vols.push_back( u->GetImplicitVol( k, maturity ) );
    }

    //! integrate the 1/K^2-weighted OTM strip into the fair variance, then assemble
    //! PV = notional * DF * (fair_var - strike_var); vega is reported w.r.t. vol
    double k_fair = VarSwap_FairVariance( fwd, t, df, strikes, vols );
    double k_var = Swap->GetVolatilityStrike() * Swap->GetVolatilityStrike(); //!< strike vol -> strike variance

    out.premium = VarSwap_Price( Swap->GetNotional(), df, k_fair, k_var );
    out.vega_bs = VarSwap_Vega( Swap->GetNotional(), df, k_fair );
    out.delta = 0; //!< first-order spot-neutral by construction (model-free strip)
    out.gamma = 0;
}
