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

//! --- security / resource caps on YAML-driven sizes -------------------------
//! The engine parses UNTRUSTED YAML directly (it is the trust boundary behind
//! the web BFF), so every size that reaches an allocation or a compute loop is
//! bounded here. These ceilings are far above any legitimate book yet stop a
//! single crafted request from OOM-ing the process or holding the global
//! pricing lock indefinitely. A value outside (0, cap] is a clean load error.
inline constexpr int PDE_MAX_GRID_NODES = 20000;                  //!< custom_n_s / custom_n_t upper bound
inline constexpr long MCL_MAX_PATHS = 200000000;                  //!< Monte-Carlo paths upper bound (2e8)
inline constexpr int CORRELATION_MAX_DIM = 2000;                  //!< correlation matrix dimension (n x n)
inline constexpr int MAX_OBJECT_REFERENCE_DEPTH = 512;            //!< reference-resolution recursion guard
inline constexpr double SERVER_PRICING_DEADLINE_SEC = 300;        //!< per-request wall-clock ceiling
inline constexpr size_t SERVER_MAX_BODY_BYTES = 16 * 1024 * 1024; //!< /price request body cap (16 MB)

//! the relative spot bump for delta/gamma (1%). One canonical value, used two
//! ways: the bump-and-revalue engine (Pricer::BumpAndRevalueGreeks) applies it
//! one-sided (S -> S*(1+bump)); the PDE grid-read delta and the analytic barrier
//! finite-difference apply it as a central half-bump (S*(1 +/- bump/2)). Lives in
//! constants.hpp (not pricer.hpp) so the contracts can use it too.
inline constexpr double GREEK_SPOT_BUMP = 0.01;

// mcl_configuration
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