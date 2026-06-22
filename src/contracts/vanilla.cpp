#include "thoth.hpp"
#include "vanilla.hpp"
#include "enums.hpp"
#include "object_reader.hpp"
#include "single.hpp" //!< Volatility / StochasticVolParams (MonoVol helper)

//! Vanilla option implementation: configuration, schedule, the analytic European
//! closed form (Black-Scholes, or the Heston characteristic-function price under
//! stochastic vol), the GPU-GBM parameter gate and the Monte-Carlo flow node.

namespace
{
//! the single's volatility iff the underlying is a mono (exactly one single name),
//! else null. Used to detect a stochastic-vol (Heston) model and to route to the
//! single-asset closed form / GPU kernel.
Volatility* MonoVol( Underlying* u )
{
    SingleSet s = u->GetSingleSet();
    return ( s.size() == 1 ) ? ( *s.begin() )->GetVolatility() : nullptr;
}
} // namespace

// constructor
Vanilla::Vanilla( const string& ObjectName ) : Contract( ObjectName, KIND_VANILLA )
{
}

Vanilla::~Vanilla() = default;

//! read own fields, then the common contract attributes
void Vanilla::Configure( ObjectReader& reader )
{
    Contract::Configure( reader ); //!< common fields first (underlying, premium currency)
    _strike = reader.Get<double>( "strike" );
    _exercise_mode = ParseExerciseMode( reader.Get<string>( "exercise" ) );
    _maturity_date = reader.Get<date>( "maturity" );
    _type = ParseOptionType( reader.Get<string>( "type" ) );
}


//! getter
double Vanilla::GetStrike() const
{
    return _strike;
}

//! getter
date Vanilla::GetMaturityDate() const
{
    return _maturity_date;
}

//! single spot fixing at maturity
set<date> Vanilla::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! single cash flow at maturity
set<date> Vanilla::GetFlowDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! terminal call/put payoff max(phi(S-K),0); the trailing payoff_vanilla flags
//! select a plain (un-digital, un-capped) vanilla
double Vanilla::Intrinsic( const double Spot )
{
    return payoff_vanilla( Spot, _strike, _type, false, 0, true, 0 );
}

//! pde solution is possible iff the underlying admits a spot grid
bool Vanilla::PDE_HasSolution()
{
    return _underlying->IsGriddable();
}

//! analytical formula is possible only for European exercise on a griddable underlying
//! (American has no closed form -> PDE/MCL only)
bool Vanilla::ANA_HasSolution()
{
    return ( _exercise_mode == ExerciseMode::European ) &&
           _underlying->IsGriddable();
}

//! bs formula
void Vanilla::ANA_EvalPrice()
{
    //! bs inputs: year fraction, discount factor, drift-carrying forward, implied
    //! vol at this strike/maturity, and the strike itself
    double t = YearFraction( _today, _maturity_date );
    double df = _premium_currency->GetRate()->GetDiscountFactor( _maturity_date );
    double f = _underlying->GetForward( _maturity_date, _premium_currency );
    double v = _underlying->GetImplicitVol( _strike, _maturity_date );
    double k = _strike;

    //! Heston stochastic vol : closed-form via the characteristic function. The
    //! forward f already carries the carry/quanto drift; discount with df. Greeks
    //! come from the per-contract bump-and-revalue (this reprices with bumps), so
    //! the analytic BS Greeks below are left zero.
    Volatility* mvol = MonoVol( _underlying );
    if ( mvol && mvol->IsStochastic() )
    {
        const StochasticVolParams h = mvol->StochasticParams();
        _valuation.premium = ( _type == OptionType::Call )
                                 ? Heston_Call_Price( f, k, t, df, h.v0, h.kappa, h.theta, h.xi, h.rho,
                                                      h.jump_intensity, h.jump_mean, h.jump_vol )
                                 : Heston_Put_Price( f, k, t, df, h.v0, h.kappa, h.theta, h.xi, h.rho,
                                                     h.jump_intensity, h.jump_mean, h.jump_vol );
        _valuation.delta = _valuation.gamma = _valuation.vega_bs = _valuation.volga_bs = 0;
        return;
    }

    //! deterministic-vol Black-Scholes: closed-form premium + delta per option type
    //! call
    if ( _type == OptionType::Call )
    {
        _valuation.premium = BS_Call_Price( f, k, t, v, df );
        _valuation.delta = BS_Call_Delta( f, k, t, v, df );
    }
    //! put
    else
    {
        _valuation.premium = BS_Put_Price( f, k, t, v, df );
        _valuation.delta = BS_Put_Delta( f, k, t, v, df );
    }

    //! greeks that are sign-independent of call/put (gamma, vega, volga)
    _valuation.gamma = BS_Gamma( f, k, t, v, df );
    _valuation.vega_bs = BS_Vega( f, k, t, v, df );
    _valuation.volga_bs = BS_Volga( f, k, t, v, df );
}

//! gpu monte-carlo (mcl_gpu): genuine single-asset GBM only — a European equity
//! vanilla with a deterministic implied vol. The forward-measure scalars mirror
//! ANA_EvalPrice, so the GPU MC reproduces the analytic BS price within MC error.
//! American / stochastic-vol (Heston) / multi-asset return false -> CPU fallback.
bool Vanilla::GPU_GbmParams( GpuGbmParams& Out )
{
    if ( _exercise_mode != ExerciseMode::European )
    {
        return false;
    }
    if ( !_underlying->IsMono() )
    {
        return false; //!< composite / basket need a multi-asset kernel (later step)
    }
    if ( Volatility* mvol = MonoVol( _underlying ); mvol && mvol->IsStochastic() )
    {
        return false; //!< stochastic vol needs the QE / CF path, not lognormal GBM
    }

    //! fill the GBM scalars (same conventions as ANA_EvalPrice) for the GPU kernel
    Out.t = YearFraction( _today, _maturity_date );
    Out.df = _premium_currency->GetRate()->GetDiscountFactor( _maturity_date );
    Out.forward = _underlying->GetForward( _maturity_date, _premium_currency );
    Out.vol = _underlying->GetImplicitVol( _strike, _maturity_date );
    Out.strike = _strike;
    Out.is_call = ( _type == OptionType::Call );
    return true;
}

//! American iff configured so (drives the LSM / PDE early-exercise path)
bool Vanilla::IsAmerican()
{
    return _exercise_mode == ExerciseMode::American;
}

//! Build (or fetch) the Monte-Carlo flow node: a call/put payoff (floored at 0)
//! on the spot node, settling at maturity.
MonteCarloNode* Vanilla::GetFlowNode( NodeCollector& NC,
                                      const date& /*AsOfDate*/ )
{

    return NC.GetOrCreate<VanillaFlowNode>( _name + node_name::FLOW,
                                            [&]( VanillaFlowNode* C )
                                            {
                                                C->SetFloor( 0 );
                                                C->SetType( _type );
                                                C->SetStrike( _strike );
                                                C->SetSpotNode( GetUnderlyingNode( NC ) );
                                                C->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );
                                            } );
}