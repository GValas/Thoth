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

//! Terminal payoff of a European binary/digital struck at `strike`: cash-or-nothing
//! pays `cash_amount`, asset-or-nothing pays `spot`, in both cases iff the option is
//! in the money (spot > strike for a call, spot < strike for a put). Not discounted.
double payoff_digital_option( const double spot,
                              const double strike,
                              const OptionType type,
                              const bool is_cash,
                              const double cash_amount );

//! Black-Scholes European digital price (forward measure): with
//!   d1 = (ln(F/K) + 0.5 v^2 t) / (v sqrt t),  d2 = d1 - v sqrt t,
//! cash-or-nothing call = CashAmount * df * N(d2), put = CashAmount * df * N(-d2);
//! asset-or-nothing call = df * F * N(d1), put = df * F * N(-d1). CashAmount is the
//! fixed payout Q (ignored for asset-or-nothing). Degenerate inputs (v<=0, t<=0,
//! F<=0 or K<=0) fall back to the discounted intrinsic on the forward.
double BS_Digital_Price( const double Forward,
                         const double Strike,
                         const double TimeToMaturity,
                         const double Volatility,
                         const double DiscountFactor,
                         const bool is_call,
                         const bool is_cash,
                         const double CashAmount );

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

//! Reiner-Rubinstein continuously-monitored single-barrier price, zero rebate, via
//! method-of-images on the Black-Scholes density (Haug, "Option Pricing Formulas").
//! Knock-out is recovered from knock-in by in/out parity (vanilla = in + out), so
//! one set of formulas covers all eight types. Parametrised in (S, r, b) like the
//! Reiner-Rubinstein reference rather than (forward, df); df discounts the payoff.
//! The contract's flavour is passed as flags so the formula stays engine-agnostic:
//!   H        active barrier level     K        strike
//!   is_call  call vs put              is_down  down- vs up-barrier
//!   is_in    knock-in vs knock-out
double Barrier_Price( const double Spot,
                      const double Rate,
                      const double CostOfCarry,
                      const double Volatility,
                      const double TimeToMaturity,
                      const double DiscountFactor,
                      const double Barrier,
                      const double Strike,
                      const bool is_call,
                      const bool is_down,
                      const bool is_in );

//! Variance swap, analytic (static replication). Fair (annualised) variance by the
//! Demeterfi-Derman-Kamal-Zou out-of-the-money option strip:
//!   K_fair = (2 / (T * df)) * integral OTM(K) / K^2 dK   (puts below F, calls above)
//! integrated by the trapezoidal rule over the caller-supplied strike grid and its
//! matching per-strike implied vols (ascending strikes > 0; the grid may be
//! non-uniform). The caller (variance.cpp) builds the grid from the surface.
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

//! Invert a call price back to its Black-Scholes implied volatility by a
//! safeguarded Newton-Raphson (the Newton step is kept inside a shrinking
//! [lo, hi] bracket and degrades to bisection when it overshoots or vega
//! collapses — deep OTM/ITM targets). Returns the annualised lognormal sigma.
//! Throws (ERR) when the price violates the no-arbitrage bounds
//! (df*(F-K)+, df*F) — no implied vol exists there.
double BS_Call_ImplicitVol( const double Forward,
                            const double Strike,
                            const double TimeToMaturity,
                            const double Price,
                            const double DiscountFactor );

//! Newton-Raphson implied-vol solver parameters
inline constexpr double INITIAL_IMPLICIT_VOL = 0.3;     //!< warm start (30% vol)
inline constexpr double IMPLICIT_VOL_MAX_ERROR = 1.e-8; //!< price-error tolerance (was 1e-30: never met)
inline constexpr int IMPLICIT_VOL_MAX_ITERATIONS = 60;  //!< enough for pure bisection on [0, MAX]
inline constexpr double IMPLICIT_VOL_MAX = 20.0;        //!< bracket cap (2000% vol; any real price sits below)

//! Analytic basket (moment-matched) proxy call prices: the basket's true distribution
//! is matched to a tractable proxy (inverse-gamma / lognormal / shifted-lognormal) and
//! priced in closed form. The moment computation and the moment -> proxy-parameter
//! inversion live in maths.hpp (LN_to_M4 / M*_to_*); these take the proxy parameters.

//! Inverse-gamma proxy call (2-moment match). Alpha/Beta are the IG shape/scale
//! (from M2_to_IG); df discounts the payoff.
double IG_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Alpha,
                      const double Beta );

//! Lognormal proxy call (2-moment match) = Black-76 in total-variance form. Mu is
//! unused (the forward pins the mean); Var is the lognormal variance (from M2_to_LN).
double LN_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Mu,
                      const double Var );

//! Shifted-lognormal proxy call (3-moment match): underlying is D + lognormal(Mu, Var),
//! so the shift D injects skew. Mu/Var/D come from M3_to_SLN.
double SLN_Call_Price( const double Forward,
                       const double Strike,
                       const double DiscountFactor,
                       const double Mu,
                       const double Var,
                       const double D );