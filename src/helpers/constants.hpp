#pragma once
// misc
inline constexpr double NB_OF_DAYS_A_YEAR = 365;
inline constexpr double NB_OF_BUSINESS_DAYS_A_YEAR = 260;

inline constexpr int DECIMAL_PRECISION = 9;
inline constexpr char ROOT_NODE[] = "root";

//! the relative spot bump for delta/gamma (1%). One canonical value, used two
//! ways: the bump-and-revalue engine (Pricer::BumpAndRevalueGreeks) applies it
//! one-sided (S -> S*(1+bump)); the PDE grid-read delta and the analytic barrier
//! finite-difference apply it as a central half-bump (S*(1 +/- bump/2)). Lives in
//! constants.hpp (not pricer.hpp) so the contracts can use it too.
inline constexpr double GREEK_SPOT_BUMP = 0.01;

// pricer_configuration - mcl
inline constexpr double NON_WORKING_DAYS_WEIGHT = 1.;

//! contracts
inline constexpr char TYPE_CALL[] = "call";
inline constexpr char TYPE_PUT[] = "put";
inline constexpr char EXERCISE_MODE_AMERICAN[] = "american";
inline constexpr char EXERCISE_MODE_EUROPEAN[] = "european";
inline constexpr char BARRIER_TYPE_UP_AND_OUT[] = "up&out";
inline constexpr char BARRIER_TYPE_UP_AND_IN[] = "up&in";
inline constexpr char BARRIER_TYPE_DOWN_AND_OUT[] = "down&out";
inline constexpr char BARRIER_TYPE_DOWN_AND_IN[] = "down&in";
inline constexpr char BARRIER_MONITORING_CONTINUOUS[] = "continuous_monitoring";
inline constexpr char BARRIER_MONITORING_DISCRETE[] = "discrete_monitoring";
inline constexpr char RAINBOW_TYPE_BEST_OF[] = "best_of";
inline constexpr char RAINBOW_TYPE_WORST_OF[] = "worst_of";