#pragma once
#include "constants.hpp"
#include <stdexcept>
#include <string>

//! Strongly-typed contract vocabulary. Config strings are parsed once (at object
//! construction) into these enums, so the hot pricing paths compare integers
//! instead of strings, and an unknown value fails loudly at parse time.

//! Option right: a call pays max(S-K,0), a put pays max(K-S,0) at exercise.
enum class OptionType
{
    Call, //!< right to BUY the underlying at the strike
    Put   //!< right to SELL the underlying at the strike
};

//! When the holder may exercise the option.
enum class ExerciseMode
{
    European, //!< exercise only at maturity
    American  //!< exercise at any time up to maturity (priced by the LSM / PDE engines)
};

//! Knock direction and effect of a single-barrier option. "Up/Down" is the side
//! of spot the barrier sits, "In/Out" whether crossing activates or kills it.
enum class BarrierType
{
    UpAndOut,   //!< alive until spot rises through the upper barrier (then knocked out)
    UpAndIn,    //!< inactive until spot rises through the upper barrier (then knocked in)
    DownAndOut, //!< alive until spot falls through the lower barrier (then knocked out)
    DownAndIn   //!< inactive until spot falls through the lower barrier (then knocked in)
};

//! How often the barrier is tested for a breach.
enum class BarrierMonitoring
{
    Continuous, //!< monitored at every instant (closed-form / Brownian-bridge corrected MC)
    Discrete    //!< monitored only on the fixing schedule (e.g. daily closes)
};

//! rainbow (best-of / worst-of) basket flavour: the payoff references the
//! best- or worst-performing asset of the basket.
enum class RainbowType
{
    BestOf, //!< payoff on the highest-performing underlying
    WorstOf //!< payoff on the lowest-performing underlying
};

//! ---- parsing (config string -> enum), failing loudly on an unknown value ----
//! (throws std::runtime_error, the same type ERR throws, so callers/tests see no
//! difference; kept here to avoid a circular include on Tools_Misc/Thoth)

//! parse "call"/"put" (TYPE_CALL/TYPE_PUT) -> OptionType; throws on anything else
inline OptionType ParseOptionType( const std::string& s )
{
    if ( s == TYPE_CALL )
        return OptionType::Call;
    if ( s == TYPE_PUT )
        return OptionType::Put;
    throw std::runtime_error( "unknown option type '" + s + "' (expected 'call' or 'put')" );
}

//! parse "european"/"american" -> ExerciseMode; throws on anything else
inline ExerciseMode ParseExerciseMode( const std::string& s )
{
    if ( s == EXERCISE_MODE_EUROPEAN )
        return ExerciseMode::European;
    if ( s == EXERCISE_MODE_AMERICAN )
        return ExerciseMode::American;
    throw std::runtime_error( "unknown exercise mode '" + s + "' (expected 'european' or 'american')" );
}

//! parse up&out / up&in / down&out / down&in -> BarrierType; throws on anything else
inline BarrierType ParseBarrierType( const std::string& s )
{
    if ( s == BARRIER_TYPE_UP_AND_OUT )
        return BarrierType::UpAndOut;
    if ( s == BARRIER_TYPE_UP_AND_IN )
        return BarrierType::UpAndIn;
    if ( s == BARRIER_TYPE_DOWN_AND_OUT )
        return BarrierType::DownAndOut;
    if ( s == BARRIER_TYPE_DOWN_AND_IN )
        return BarrierType::DownAndIn;
    throw std::runtime_error( "unknown barrier type '" + s +
                              "' (expected up&out / up&in / down&out / down&in)" );
}

//! parse continuous_monitoring / discrete_monitoring -> BarrierMonitoring; throws otherwise
inline BarrierMonitoring ParseBarrierMonitoring( const std::string& s )
{
    if ( s == BARRIER_MONITORING_CONTINUOUS )
        return BarrierMonitoring::Continuous;
    if ( s == BARRIER_MONITORING_DISCRETE )
        return BarrierMonitoring::Discrete;
    throw std::runtime_error( "unknown barrier monitoring '" + s +
                              "' (expected 'continuous_monitoring' or 'discrete_monitoring')" );
}

//! parse "best_of"/"worst_of" -> RainbowType; throws on anything else
inline RainbowType ParseRainbowType( const std::string& s )
{
    if ( s == RAINBOW_TYPE_BEST_OF )
        return RainbowType::BestOf;
    if ( s == RAINBOW_TYPE_WORST_OF )
        return RainbowType::WorstOf;
    throw std::runtime_error( "unknown rainbow type '" + s + "' (expected 'best_of' or 'worst_of')" );
}

//! ---- convenience predicates ----

//! true for the knock-IN variants (the option only becomes alive once breached);
//! false for the knock-OUT variants. Lets the pricer branch on the in/out logic.
inline bool IsKnockIn( BarrierType b )
{
    return b == BarrierType::UpAndIn || b == BarrierType::DownAndIn;
}
//! true when the barrier sits ABOVE spot (up-barrier), false for a down-barrier;
//! selects which side spot must cross to trigger the knock.
inline bool IsUpBarrier( BarrierType b )
{
    return b == BarrierType::UpAndOut || b == BarrierType::UpAndIn;
}
