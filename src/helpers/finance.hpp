#pragma once
#include "thoth.hpp"

double payoff_vanilla( const double spot,
                       const double strike,
                       const OptionType type,
                       const bool has_cap,
                       const double cap,
                       const bool has_floor,
                       const double floor );

double payoff_digital( const double spot,
                       const string& barrier_type,
                       const double barrier_up_level,
                       const double barrier_down_level );

//! bs call price
double BS_Call_Price( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor );

//! bs put price, call/put parity
double BS_Put_Price( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor );

//! bs call delta
double BS_Call_Delta( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor );

//! bs put delta, call/put parity
double BS_Put_Delta( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor );

//! bs vega
double BS_Vega( const double Forward,
                const double Strike,
                const double TimeToMaturity,
                const double Volatility,
                const double DiscountFactor );

//! bs gamma
double BS_Gamma( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor );

//! bs volga
double BS_Volga( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor );

//! Heston European call/put via the characteristic function (forward measure,
//! "Little Heston Trap" branch-stable form, GSL adaptive integration). Forward
//! F already carries the carry/quanto drift; df discounts in the payoff currency.
//! v0/theta are variances, xi the vol-of-vol, rho the spot/variance correlation.
double Heston_Call_Price( const double Forward,
                          const double Strike,
                          const double TimeToMaturity,
                          const double DiscountFactor,
                          const double V0,
                          const double Kappa,
                          const double Theta,
                          const double Xi,
                          const double Rho );

double Heston_Put_Price( const double Forward,
                         const double Strike,
                         const double TimeToMaturity,
                         const double DiscountFactor,
                         const double V0,
                         const double Kappa,
                         const double Theta,
                         const double Xi,
                         const double Rho );

// implicit vol
double BS_Call_ImplicitVol( const double Forward,
                            const double Strike,
                            const double TimeToMaturity,
                            const double Price,
                            const double DiscountFactor );

//! Newton-Raphson implied-vol solver parameters
inline constexpr double INITIAL_IMPLICIT_VOL = 0.3;
inline constexpr double IMPLICIT_VOL_MAX_ERROR = 1.e-8; //!< price-error tolerance (was 1e-30: never met)
inline constexpr int IMPLICIT_VOL_MAX_ITERATIONS = 30;