#pragma once
#include "constants.hpp"
#include <stdexcept>
#include <string>

//! Strongly-typed contract vocabulary. Config strings are parsed once (at object
//! construction) into these enums, so the hot pricing paths compare integers
//! instead of strings, and an unknown value fails loudly at parse time.

enum class OptionType
{
    Call,
    Put
};

enum class ExerciseMode
{
    European,
    American
};

enum class BarrierType
{
    UpAndOut,
    UpAndIn,
    DownAndOut,
    DownAndIn
};

enum class BarrierMonitoring
{
    Continuous,
    Discrete
};

//! rainbow (best-of / worst-of) basket flavour
enum class RainbowType
{
    BestOf,
    WorstOf
};

//! ---- parsing (config string -> enum), failing loudly on an unknown value ----
//! (throws std::runtime_error, the same type ERR throws, so callers/tests see no
//! difference; kept here to avoid a circular include on Tools_Misc/Thoth)

inline OptionType ParseOptionType( const std::string& s )
{
    if ( s == TYPE_CALL )
        return OptionType::Call;
    if ( s == TYPE_PUT )
        return OptionType::Put;
    throw std::runtime_error( "unknown option type '" + s + "' (expected 'call' or 'put')" );
}

inline ExerciseMode ParseExerciseMode( const std::string& s )
{
    if ( s == EXERCISE_MODE_EUROPEAN )
        return ExerciseMode::European;
    if ( s == EXERCISE_MODE_AMERICAN )
        return ExerciseMode::American;
    throw std::runtime_error( "unknown exercise mode '" + s + "' (expected 'european' or 'american')" );
}

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

inline BarrierMonitoring ParseBarrierMonitoring( const std::string& s )
{
    if ( s == BARRIER_MONITORING_CONTINUOUS )
        return BarrierMonitoring::Continuous;
    if ( s == BARRIER_MONITORING_DISCRETE )
        return BarrierMonitoring::Discrete;
    throw std::runtime_error( "unknown barrier monitoring '" + s +
                              "' (expected 'continuous_monitoring' or 'discrete_monitoring')" );
}

inline RainbowType ParseRainbowType( const std::string& s )
{
    if ( s == RAINBOW_TYPE_BEST_OF )
        return RainbowType::BestOf;
    if ( s == RAINBOW_TYPE_WORST_OF )
        return RainbowType::WorstOf;
    throw std::runtime_error( "unknown rainbow type '" + s + "' (expected 'best_of' or 'worst_of')" );
}

//! ---- convenience predicates ----

inline bool IsKnockIn( BarrierType b )
{
    return b == BarrierType::UpAndIn || b == BarrierType::DownAndIn;
}
inline bool IsUpBarrier( BarrierType b )
{
    return b == BarrierType::UpAndOut || b == BarrierType::UpAndIn;
}
