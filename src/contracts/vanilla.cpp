#include "thoth.hpp"
#include "vanilla.hpp"

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
double Vanilla::GetStrike()
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
double Vanilla::PDE_EvalFlow( const double Spot )
{
    return payoff_vanilla( Spot, _strike, _type, false, 0, true, 0 );
}

//! pde solution is possible
bool Vanilla::PDE_HasSolution()
{
    return ( _underlying->GetKind() == KIND_EQUITY ||
             _underlying->GetKind() == KIND_COMPOSITE ||
             _underlying->GetKind() == KIND_BASKET );
}

//! analytical formula is possible
bool Vanilla::ANA_HasSolution()
{
    return ( _exercise_mode == ExerciseMode::European ) &&
           ( _underlying->GetKind() == KIND_EQUITY ||
             _underlying->GetKind() == KIND_COMPOSITE ||
             _underlying->GetKind() == KIND_BASKET );
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

    //! call
    if ( _type == OptionType::Call )
    {
        _premium = BS_Call_Price( f, k, t, v, df );
        _delta = BS_Call_Delta( f, k, t, v, df );
    }
    //! put
    else
    {
        _premium = BS_Put_Price( f, k, t, v, df );
        _delta = BS_Put_Delta( f, k, t, v, df );
    }

    //! common greeks
    _gamma = BS_Gamma( f, k, t, v, df );
    _vega_bs = BS_Vega( f, k, t, v, df );
    _volga_bs = BS_Volga( f, k, t, v, df );
}

//
bool Vanilla::PDE_IsAmerican()
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