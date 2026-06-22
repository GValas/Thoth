#pragma once
#include "thoth.hpp"

//! ----------------------------------------------------------------------
//! Closed-form / semi-analytic financial primitives: payoff functions, the
//! Black-Scholes price & Greeks, the analytic variance-swap (DDKZ replication)
//! and the Heston/Bates characteristic-function European pricer. All work in the
//! FORWARD measure: the caller passes a Forward already carrying the carry/quanto
//! drift and a DiscountFactor that discounts the payoff currency, so no rate
//! appears explicitly here. These are the analytic references the MC/PDE engines
//! are validated against and the building blocks for the moment-matched basket
//! pricers in maths.cpp.
//! ----------------------------------------------------------------------

//! Terminal payoff of a (possibly capped/floored) vanilla, given the realised
//! spot. Returns the INTRINSIC value spot-strike (call) or strike-spot (put),
//! optionally clamped to [floor, cap]; not discounted, not flooring at 0 unless a
//! floor=0 is supplied (callers pass the floor explicitly).
double payoff_vanilla( const double spot,
                       const double strike,
                       const OptionType type,
                       const bool has_cap,
                       const double cap,
                       const bool has_floor,
                       const double floor );

//! Terminal payoff of a cash-or-nothing digital (pays 1 or 0) for the given
//! barrier flavour, comparing realised spot against the up/down barrier level.
//! barrier_type is the raw config string (BARRIER_TYPE_* constants).
double payoff_digital( const double spot,
                       const string& barrier_type,
                       const double barrier_up_level,
                       const double barrier_down_level );

//! Black-Scholes-76 (forward-measure) call price = df * (F*N(d1) - K*N(d2)).
//! Volatility is annualised lognormal sigma, TimeToMaturity in years, F & K in
//! price units. Degenerate inputs (sigma<=0, T==0, ...) fall back to discounted
//! intrinsic.
double BS_Call_Price( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor );

//! BS put price via put/call parity: P = C - df*(F - K).
double BS_Put_Price( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor );

//! BS call delta w.r.t. the FORWARD = df * N(d1) (forward delta, not spot delta).
double BS_Call_Delta( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor );

//! BS put delta via parity: call delta - 1.
double BS_Put_Delta( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor );

//! BS vega = dPrice/dsigma (per unit vol, i.e. 1.00 = 100 vol points), same for
//! call and put. Returns 0 on degenerate inputs.
double BS_Vega( const double Forward,
                const double Strike,
                const double TimeToMaturity,
                const double Volatility,
                const double DiscountFactor );

//! BS gamma = d2Price/dForward^2 (curvature w.r.t. the forward), same call/put.
double BS_Gamma( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor );

//! BS volga (vomma) = d2Price/dsigma^2, the vega convexity; used to gauge the
//! smile sensitivity of the vol marks.
double BS_Volga( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor );

//! Variance swap, analytic (static replication). Fair (annualised) variance by the
//! Demeterfi-Derman-Kamal-Zou out-of-the-money option strip:
//!   K_fair = (2 / (T * df)) * integral OTM(K) / K^2 dK   (puts below F, calls above)
//! integrated by the trapezoidal rule over the caller-supplied strike grid and its
//! matching per-strike implied vols (ascending strikes > 0; the grid may be
//! non-uniform). The caller (variance_swap.cpp) builds the grid from the surface.
double VarSwap_FairVariance( const double Forward,
                             const double TimeToMaturity,
                             const double DiscountFactor,
                             const vector<double>& Strikes,
                             const vector<double>& Vols );

//! variance-swap present value: notional * df * (fair_var - strike_var)
double VarSwap_Price( const double Notional,
                      const double DiscountFactor,
                      const double FairVariance,
                      const double StrikeVariance );

//! variance-swap BS vega (dPV/dvol): notional * df * 2 * sqrt(fair_var)
double VarSwap_Vega( const double Notional,
                     const double DiscountFactor,
                     const double FairVariance );

//! Heston European call/put via the characteristic function (forward measure,
//! "Little Heston Trap" branch-stable form, GSL adaptive integration). Forward
//! F already carries the carry/quanto drift; df discounts in the payoff currency.
//! v0/theta are variances, xi the vol-of-vol, rho the spot/variance correlation.
//! The optional lognormal-jump parameters (intensity lambda, log-jump mean mu and
//! vol sigma) turn this into the Bates model: the characteristic function gets an
//! extra closed-form jump factor. All zero -> pure Heston. Requires Xi > 0.
double Heston_Call_Price( const double Forward,
                          const double Strike,
                          const double TimeToMaturity,
                          const double DiscountFactor,
                          const double V0,
                          const double Kappa,
                          const double Theta,
                          const double Xi,
                          const double Rho,
                          const double JumpIntensity = 0,
                          const double JumpMean = 0,
                          const double JumpVol = 0 );

double Heston_Put_Price( const double Forward,
                         const double Strike,
                         const double TimeToMaturity,
                         const double DiscountFactor,
                         const double V0,
                         const double Kappa,
                         const double Theta,
                         const double Xi,
                         const double Rho,
                         const double JumpIntensity = 0,
                         const double JumpMean = 0,
                         const double JumpVol = 0 );

//! Invert a call price back to its Black-Scholes implied volatility by
//! Newton-Raphson (step = price_error / vega). Returns the annualised lognormal
//! sigma. Throws (ERR) if vega collapses to zero so it never reports a bogus vol.
double BS_Call_ImplicitVol( const double Forward,
                            const double Strike,
                            const double TimeToMaturity,
                            const double Price,
                            const double DiscountFactor );

//! Newton-Raphson implied-vol solver parameters
inline constexpr double INITIAL_IMPLICIT_VOL = 0.3;     //!< warm start (30% vol)
inline constexpr double IMPLICIT_VOL_MAX_ERROR = 1.e-8; //!< price-error tolerance (was 1e-30: never met)
inline constexpr int IMPLICIT_VOL_MAX_ITERATIONS = 30;