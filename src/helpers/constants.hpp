#pragma once
// misc
const double NB_OF_DAYS_A_YEAR = 365;
const double NB_OF_BUSINESS_DAYS_A_YEAR = 260;

const int DECIMAL_PRECISION = 9;
inline constexpr char ROOT_NODE[] = "root";
const double GREEK_SPOT_SHIFT = 0.01;

// pricer_configuration - mcl
const double NON_WORKING_DAYS_WEIGHT = 1.;

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