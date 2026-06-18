#include "thoth.hpp"
#include "vanilla.hpp"

namespace
{
//! the single's volatility if the underlying is a mono (one single), else null
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

//! setter
void Vanilla::SetStrike( const double Strike )
{
    _strike = Strike;
}

//! setter
void Vanilla::SetMaturityDate( const date& MaturityDate )
{
    _maturity_date = MaturityDate;
}

//! setter
void Vanilla::SetExerciseMode( ExerciseMode Mode )
{
    _exercise_mode = Mode;
}

//! setter
void Vanilla::SetType( OptionType Type )
{
    _type = Type;
}

//! getter
double Vanilla::GetStrike() const
{
    return _strike;
}

//! getter
date Vanilla::GetMaturityDate()
{
    return _maturity_date;
}

set<date> Vanilla::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

set<date> Vanilla::GetFlowDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

set<date> Vanilla::GetAmericanExerciseDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//!
double Vanilla::Intrinsic( const double Spot )
{
    return payoff_vanilla( Spot, _strike, _type, false, 0, true, 0 );
}

//! pde solution is possible
bool Vanilla::PDE_HasSolution()
{
    return _underlying->IsGriddable();
}

//! analytical formula is possible
bool Vanilla::ANA_HasSolution()
{
    return ( _exercise_mode == ExerciseMode::European ) &&
           _underlying->IsGriddable();
}

//! bs formula
void Vanilla::ANA_EvalPrice()
{
    //! bs inputs*
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

    //! common greeks
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

    Out.t = YearFraction( _today, _maturity_date );
    Out.df = _premium_currency->GetRate()->GetDiscountFactor( _maturity_date );
    Out.forward = _underlying->GetForward( _maturity_date, _premium_currency );
    Out.vol = _underlying->GetImplicitVol( _strike, _maturity_date );
    Out.strike = _strike;
    Out.is_call = ( _type == OptionType::Call );
    return true;
}

//
bool Vanilla::IsAmerican()
{
    return _exercise_mode == ExerciseMode::American;
}

MonteCarloNode* Vanilla::GetFlowNode( NodeCollector& NC,
                                      const date& /*AsOfDate*/ )
{

    return NC.GetOrCreate<VanillaFlowNode>( _name + "#flow",
                                            [&]( VanillaFlowNode* C )
                                            {
                                                C->SetFloor( 0 );
                                                C->SetType( _type );
                                                C->SetStrike( _strike );
                                                C->SetSpotNode( GetUnderlyingNode( NC ) );
                                                C->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );
                                            } );
}