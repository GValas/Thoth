#pragma once
//! Project-wide magic numbers and config-string literals, gathered in one place
//! so the contracts, pricers and config parser share a single source of truth.

// misc
//! calendar days per year — the ACT/365 day-count denominator (see YearFraction)
inline constexpr double NB_OF_DAYS_A_YEAR = 365;
//! business (trading) days per year — used when a quantity is quoted/accrued on a
//! trading-day basis rather than calendar time (e.g. variance-swap conventions)
inline constexpr double NB_OF_BUSINESS_DAYS_A_YEAR = 260;

//! significant digits when serialising a double to a string (ToString) — wide
//! enough to round-trip prices/vols without dumping full binary noise
inline constexpr int DECIMAL_PRECISION = 9;
//! name of the root node in the object/config tree
inline constexpr char ROOT_NODE[] = "root";

//! the relative spot bump for delta/gamma (1%). One canonical value, used two
//! ways: the bump-and-revalue engine (Pricer::BumpAndRevalueGreeks) applies it
//! one-sided (S -> S*(1+bump)); the PDE grid-read delta and the analytic barrier
//! finite-difference apply it as a central half-bump (S*(1 +/- bump/2)). Lives in
//! constants.hpp (not pricer.hpp) so the contracts can use it too.
inline constexpr double GREEK_SPOT_BUMP = 0.01;

// pricer_configuration - mcl
//! Monte-Carlo / cluster (mcl): weight given to a non-working (calendar, non-
//! trading) day when accumulating time/variance. 1.0 = treat it like any other
//! day (no calendar-vs-trading-day down-weighting).
inline constexpr double NON_WORKING_DAYS_WEIGHT = 1.;

//! contracts — canonical config-string spellings, parsed once into the strongly
//! typed enums in enums.hpp. Keeping the literals here (not inline in the parser)
//! lets the YAML loader and the enum parser agree on exactly one spelling each.
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