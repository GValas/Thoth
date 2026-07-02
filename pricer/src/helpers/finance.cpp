#include "finance.hpp"
#include "distributions.hpp"
#include <complex>
#include <limits>

#include <boost/math/quadrature/gauss_kronrod.hpp>

//! vanilla price
double payoff_vanilla( const double spot,
                       const double strike,
                       const OptionType type,
                       const bool has_cap,
                       const double cap,
                       const bool has_floor,
                       const double floor )
{
    //! vanilla part : signed intrinsic (not yet floored at 0; a payoff floor of 0
    //! is applied only if the caller passes has_floor with floor==0)
    double vanilla = ( type == OptionType::Call ) ? ( spot - strike ) : ( strike - spot );

    //! cap/floor : clamp the intrinsic into [floor, cap] when requested
    if ( has_cap )
    {
        vanilla = min( vanilla, cap );
    }
    if ( has_floor )
    {
        vanilla = max( vanilla, floor );
    }

    return vanilla;
}

double payoff_digital( const double spot,
                       const string& barrier_type,
                       const double barrier_up_level,
                       const double barrier_down_level )
{
    double digital;
    if ( barrier_type == BARRIER_TYPE_UP_AND_OUT )
    {
        digital = ( spot >= barrier_up_level ) ? 0 : 1;
    }
    else if ( barrier_type == BARRIER_TYPE_UP_AND_IN )
    {
        digital = ( spot >= barrier_up_level ) ? 1 : 0;
    }
    else if ( barrier_type == BARRIER_TYPE_DOWN_AND_OUT )
    {
        digital = ( spot <= barrier_down_level ) ? 0 : 1;
    }
    else if ( barrier_type == BARRIER_TYPE_DOWN_AND_IN )
    {
        digital = ( spot <= barrier_down_level ) ? 1 : 0;
    }
    else
    {
        ERR( "unknown barrier type '" + barrier_type + "'" ); //!< ERR already prefixes "ERR> "
    }
    return digital;
}

//! bs call price
double BS_Call_Price( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor )
{
    //! degenerate cases (no vol, no time, non-positive F/K): the lognormal is a
    //! point mass at F, so the price is the discounted intrinsic
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return DiscountFactor * max( Forward - Strike, 0.0 );
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity ); //!< total vol sigma*sqrt(T)
        //! d1 = [ln(F/K) + sigma^2 T/2] / (sigma sqrt(T)), d2 = d1 - sigma sqrt(T)
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double d2 = d1 - v_sqr_t;
        double Nd1 = NormalCdf( d1 );
        double Nd2 = NormalCdf( d2 );
        return DiscountFactor * ( Forward * Nd1 - Strike * Nd2 );
    }
}

//! bs put price, call/put parity
double BS_Put_Price( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor )
{
    //! put = call - df*(F - K)  (forward-measure put/call parity)
    double c = BS_Call_Price( Forward, Strike, TimeToMaturity, Volatility, DiscountFactor );
    double p = c - DiscountFactor * ( Forward - Strike );
    return p;
}

//! bs call delta
double BS_Call_Delta( const double Forward,
                      const double Strike,
                      const double TimeToMaturity,
                      const double Volatility,
                      const double DiscountFactor )
{
    //! degenerate: the option is either fully in or out of the money, so the
    //! forward delta is df (ITM) or 0 (OTM)
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return ( Forward > Strike ? DiscountFactor : 0 );
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double Nd1 = NormalCdf( d1 );
        return DiscountFactor * Nd1; //!< forward delta = df * N(d1)
    }
}

//! bs put delta, call/put parity
double BS_Put_Delta( const double Forward,
                     const double Strike,
                     const double TimeToMaturity,
                     const double Volatility,
                     const double DiscountFactor )
{
    //! put delta = call delta - 1 (differentiating parity w.r.t. the forward)
    double c = BS_Call_Delta( Forward, Strike, TimeToMaturity, Volatility, DiscountFactor );
    double p = c - 1;
    return p;
}

//! bs vega
double BS_Vega( const double Forward,
                const double Strike,
                const double TimeToMaturity,
                const double Volatility,
                const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return 0;
    }
    else
    {
        double sqr_t = sqrt( TimeToMaturity );
        double v_sqr_t = Volatility * sqr_t;
        double d2 = log( Forward / Strike ) / v_sqr_t - 0.5 * v_sqr_t;
        double Fd2 = NormalPdf( d2 );
        //! vega = df * K * sqrt(T) * phi(d2) (equivalently df * F * sqrt(T) * phi(d1))
        return DiscountFactor * Strike * sqr_t * Fd2;
    }
}

//! bs gamma
double BS_Gamma( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return 0;
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double Fd1 = NormalPdf( d1 );
        //! gamma = df * phi(d1) / (F * sigma * sqrt(T))
        return DiscountFactor * Fd1 / Forward / v_sqr_t;
    }
}

//! bs volga
double BS_Volga( const double Forward,
                 const double Strike,
                 const double TimeToMaturity,
                 const double Volatility,
                 const double DiscountFactor )
{
    if ( Volatility <= 0 || TimeToMaturity == 0 || Strike <= 0 || Forward <= 0 )
    {
        return 0;
    }
    else
    {
        double v_sqr_t = Volatility * sqrt( TimeToMaturity );
        double d1 = log( Forward / Strike ) / v_sqr_t + 0.5 * v_sqr_t;
        double d2 = d1 - v_sqr_t;
        double Fd1 = NormalPdf( d1 );
        //! volga = vega * d1 * d2 / sigma = df*F*phi(d1)*sqrt(T) * d1*d2
        //! (here vega*sqrt(T)*... is regrouped as F*phi(d1)*v_sqr_t*d1*d2)
        return DiscountFactor * Forward * Fd1 * v_sqr_t * d1 * d2;
    }
}

//! Reiner-Rubinstein continuously-monitored single-barrier price (zero rebate). See
//! the header for the parameter contract. Knock-out is recovered from knock-in via
//! in/out parity, so only the knock-in formulas are written out here.
double Barrier_Price( const double S,
                      const double r,
                      const double b,
                      const double v,
                      const double t,
                      const double df,
                      const double H,
                      const double K,
                      const bool is_call,
                      const bool is_down,
                      const bool is_in )
{
    //! vanilla reference (the same BS_*_Price the deterministic-vol path uses). Used
    //! both for the degenerate fallback and to recover the knock-out by parity.
    double fwd = S * exp( b * t );
    double vanilla = is_call ? BS_Call_Price( fwd, K, t, v, df )
                             : BS_Put_Price( fwd, K, t, v, df );

    //! degenerate inputs : the diffusion collapses, so a knock-in can never trigger
    //! (value 0) and a knock-out is the full vanilla
    if ( v <= 0 || t <= 0 || S <= 0 || H <= 0 )
    {
        return is_in ? 0.0 : vanilla;
    }

    //! barrier already breached at valuation: a knock-in is now a live vanilla, a
    //! knock-out is dead (value 0)
    bool breached = is_down ? ( S <= H ) : ( S >= H );
    if ( breached )
    {
        return is_in ? vanilla : 0.0;
    }

    //! Reiner-Rubinstein building blocks (Haug, "Option Pricing Formulas").
    //! phi = call/put sign, eta = down/up sign, mu the drift in log-moneyness units.
    double phi = is_call ? 1.0 : -1.0;
    double eta = is_down ? 1.0 : -1.0;
    double sqt = v * sqrt( t );                  //!< total std dev to maturity
    double mu = ( b - 0.5 * v * v ) / ( v * v ); //!< drift / variance (per-unit-time)

    //! the four standardised log-distances; x* measure to the strike/barrier, y* are
    //! their images reflected across the barrier (the method-of-images terms)
    double x1 = log( S / K ) / sqt + ( 1 + mu ) * sqt;
    double x2 = log( S / H ) / sqt + ( 1 + mu ) * sqt;
    double y1 = log( H * H / ( S * K ) ) / sqt + ( 1 + mu ) * sqt;
    double y2 = log( H / S ) / sqt + ( 1 + mu ) * sqt;

    double Sbr = S * exp( ( b - r ) * t ); //!< S e^{(b-r)t} = fwd * df
    double Kdf = K * df;
    double pHS_p1 = pow( H / S, 2 * ( mu + 1 ) ); //!< image reflection weight (asset leg)
    double pHS_m = pow( H / S, 2 * mu );          //!< image reflection weight (cash leg)

    //! A,B = plain truncated-vanilla terms; C,D = their barrier-reflected images.
    //! Each is asset leg (Sbr * N) minus cash leg (Kdf * N), in Haug's notation.
    auto A = [&]()
    { return phi * Sbr * NormalCdf( phi * x1 ) - phi * Kdf * NormalCdf( phi * ( x1 - sqt ) ); };
    auto B = [&]()
    { return phi * Sbr * NormalCdf( phi * x2 ) - phi * Kdf * NormalCdf( phi * ( x2 - sqt ) ); };
    auto C = [&]()
    { return phi * Sbr * pHS_p1 * NormalCdf( eta * y1 ) - phi * Kdf * pHS_m * NormalCdf( eta * ( y1 - sqt ) ); };
    auto D = [&]()
    { return phi * Sbr * pHS_p1 * NormalCdf( eta * y2 ) - phi * Kdf * pHS_m * NormalCdf( eta * ( y2 - sqt ) ); };

    //! knock-in value (rebate = 0). The A/B/C/D combination depends on the barrier
    //! type AND on whether the strike sits beyond the barrier (K vs H), which decides
    //! how the payoff region intersects the reflected one — Haug's case table.
    double knock_in;
    if ( is_call && is_down )
    {
        knock_in = ( K > H ) ? C() : A() - B() + D();
    }
    else if ( is_call && !is_down ) //!< up
    {
        knock_in = ( K > H ) ? A() : B() - C() + D();
    }
    else if ( !is_call && is_down ) //!< down put
    {
        knock_in = ( K > H ) ? B() - C() + D() : A();
    }
    else //!< up put
    {
        knock_in = ( K > H ) ? A() - B() + D() : C();
    }

    //! knock-out by in/out parity: vanilla = knock_in + knock_out
    return is_in ? knock_in : vanilla - knock_in;
}

//! fair (annualised) variance: trapezoidal integral of the OTM-option strip
//! g(K) = OTM(K)/K^2 over the caller-supplied strike grid (puts below the forward,
//! calls above), scaled by 2/(T*df). The grid may be non-uniform.
double VarSwap_FairVariance( const double Forward,
                             const double TimeToMaturity,
                             const double DiscountFactor,
                             const vector<double>& Strikes,
                             const vector<double>& Vols )
{
    if ( TimeToMaturity <= 0 || Strikes.size() < 2 || Strikes.size() != Vols.size() )
    {
        return 0;
    }

    //! integrand g(K) = OTM(K) / K^2 at each strike (the prices are discounted, so
    //! e^{rT} = 1/df is applied via the 2/(T*df) factor below)
    vector<double> g( Strikes.size() );
    for ( size_t i = 0; i < Strikes.size(); i++ )
    {
        const double k = Strikes[i];
        const double price = ( k < Forward ) ? BS_Put_Price( Forward, k, TimeToMaturity, Vols[i], DiscountFactor )
                                             : BS_Call_Price( Forward, k, TimeToMaturity, Vols[i], DiscountFactor );
        g[i] = price / ( k * k );
    }

    //! trapezoidal rule over the (possibly non-uniform) strike grid
    double integral = 0;
    for ( size_t i = 0; i + 1 < Strikes.size(); i++ )
    {
        integral += 0.5 * ( g[i] + g[i + 1] ) * ( Strikes[i + 1] - Strikes[i] );
    }

    return ( 2.0 / ( TimeToMaturity * DiscountFactor ) ) * integral;
}

//! variance-swap present value
double VarSwap_Price( const double Notional,
                      const double DiscountFactor,
                      const double FairVariance,
                      const double StrikeVariance )
{
    return Notional * DiscountFactor * ( FairVariance - StrikeVariance );
}

//! variance-swap BS vega (dPV/dvol)
double VarSwap_Vega( const double Notional,
                     const double DiscountFactor,
                     const double FairVariance )
{
    return Notional * DiscountFactor * 2.0 * sqrt( ( FairVariance > 0 ) ? FairVariance : 0.0 );
}

// implicit vol
double BS_Call_ImplicitVol( const double Forward,
                            const double Strike,
                            const double TimeToMaturity,
                            const double Price,
                            const double DiscountFactor )
{
    //! no-arbitrage bounds: df*(F-K)+ < C < df*F. A target outside them has no
    //! implied vol at all — fail loudly instead of letting Newton wander.
    const double intrinsic = DiscountFactor * max( Forward - Strike, 0.0 );
    const double upper = DiscountFactor * Forward;
    if ( !( Price > intrinsic ) || !( Price < upper ) )
    {
        ERR( "BS_Call_ImplicitVol: price " + ToString( Price ) +
             " is outside the no-arbitrage bounds (" + ToString( intrinsic ) + ", " +
             ToString( upper ) + "), no implied vol exists" );
    }

    //! bracket [lo, hi]: the call price is strictly increasing in vol, from the
    //! intrinsic (vol -> 0) to df*F (vol -> inf). Expand hi until it over-prices
    //! the target so bisection always has a valid interval to fall back on.
    double lo = 0.0;
    double hi = 4.0 * INITIAL_IMPLICIT_VOL; //!< 120% vol covers any market case
    while ( BS_Call_Price( Forward, Strike, TimeToMaturity, hi, DiscountFactor ) < Price &&
            hi < IMPLICIT_VOL_MAX )
    {
        hi *= 2;
    }

    //! safeguarded Newton: the quadratic Newton step is taken while it stays inside
    //! the shrinking [lo, hi] bracket (near-flat vega or an overshoot would throw it
    //! out — deep OTM/ITM targets); otherwise the step degrades to a bisection, so
    //! the iteration always converges on a bracketable price.
    double vol = INITIAL_IMPLICIT_VOL;
    for ( int i = 0; i < IMPLICIT_VOL_MAX_ITERATIONS; i++ )
    {
        const double err = Price - BS_Call_Price( Forward, Strike, TimeToMaturity, vol, DiscountFactor );
        if ( abs( err ) <= IMPLICIT_VOL_MAX_ERROR )
        {
            return vol;
        }
        //! shrink the bracket around the root (price increasing in vol)
        if ( err > 0 )
        {
            lo = vol; //!< under-priced: the root is above
        }
        else
        {
            hi = vol; //!< over-priced: the root is below
        }
        const double vega = BS_Vega( Forward, Strike, TimeToMaturity, vol, DiscountFactor );
        double next = ( vega > 0 ) ? vol + err / vega : lo; //!< Newton, or force the bisection below
        if ( !( next > lo && next < hi ) )
        {
            next = 0.5 * ( lo + hi ); //!< Newton left the bracket -> bisect
        }
        vol = next;
    }
    return vol; //!< bracket midpoint after the cap: converged to bisection accuracy
}
//! ----------------------------------------------------------------------
//! Heston European pricing via the characteristic function
//! ----------------------------------------------------------------------
namespace
{
using cdouble = std::complex<double>;

//! Bundle of Heston/Bates parameters threaded through the CF integrand.
//! F forward, K strike, T maturity (years); v0 spot variance, kappa mean-reversion
//! speed, theta long-run variance, xi vol-of-vol, rho spot/variance correlation.
struct HestonParams
{
    double F, K, T, v0, kappa, theta, xi, rho;
    double lambda, muJ, sigmaJ; //!< Bates lognormal jumps (lambda = 0 -> pure Heston)
    int j;                      //!< 1 or 2 (the two Heston probabilities P1, P2)
};

//! integrand of P_j : Re( e^{-i phi ln K} f_j(phi) / (i phi) ), forward measure
//! (x = ln F, no drift term — the forward carries the drift).
double heston_integrand( double phi, void* params )
{
    const HestonParams& h = *static_cast<HestonParams*>( params );
    const cdouble I( 0.0, 1.0 );

    //! uj, bj are the standard Heston P1/P2 coefficients (Heston 1993): the j=1
    //! measure shifts the drift by -rho*xi (b1 = kappa - rho*xi), j=2 keeps kappa.
    const double uj = ( h.j == 1 ) ? 0.5 : -0.5;
    const double bj = ( h.j == 1 ) ? ( h.kappa - h.rho * h.xi ) : h.kappa;
    const double a = h.kappa * h.theta;
    const double xi2 = h.xi * h.xi;

    const cdouble rxi = h.rho * h.xi * I * phi; //!< rho*xi*i*phi, recurring subterm
    //! d = sqrt((rho*xi*i*phi - bj)^2 - xi^2*(2*uj*i*phi - phi^2)) — the CF discriminant
    const cdouble d = std::sqrt( ( rxi - bj ) * ( rxi - bj ) - xi2 * ( 2.0 * uj * I * phi - phi * phi ) );
    //! g uses the (bj - rxi - d) numerator: the "Little Heston Trap" (Albrecher et
    //! al.) form that keeps the complex log on the principal, continuous branch and
    //! avoids the discontinuities of the original (+d) parametrisation
    const cdouble g = ( bj - rxi - d ) / ( bj - rxi + d ); //!< little-Heston-trap
    const cdouble edt = std::exp( -d * h.T );

    //! C(T,phi) and D(T,phi) : the affine exponents of the Heston CF
    const cdouble C = ( a / xi2 ) * ( ( bj - rxi - d ) * h.T - 2.0 * std::log( ( 1.0 - g * edt ) / ( 1.0 - g ) ) );
    const cdouble D = ( bj - rxi - d ) / xi2 * ( ( 1.0 - edt ) / ( 1.0 - g * edt ) );
    //! characteristic function f_j(phi) = exp(C + D*v0 + i*phi*ln F)
    cdouble f = std::exp( C + D * h.v0 + I * phi * std::log( h.F ) );

    //! Bates : multiply by the closed-form jump characteristic function. The two
    //! probabilities are the CF under different measures, encoded here by the
    //! argument shift u = phi (P2) vs phi - i (P1); the compensator (-i u kbar)
    //! makes the jump factor martingale-neutral, so it equals 1 at u = -i.
    if ( h.lambda > 0 )
    {
        const cdouble u = ( h.j == 1 ) ? ( phi - I ) : cdouble( phi );
        const double kbar = exp( h.muJ + 0.5 * h.sigmaJ * h.sigmaJ ) - 1.0;
        const cdouble psi = std::exp( I * u * h.muJ - 0.5 * u * u * h.sigmaJ * h.sigmaJ ) - 1.0 - I * u * kbar;
        f *= std::exp( h.T * h.lambda * psi );
    }

    return std::real( std::exp( -I * phi * std::log( h.K ) ) * f / ( I * phi ) );
}

//! P_j = 1/2 + 1/pi * integral_0^inf integrand dphi
//! Heston/Bates characteristic-function probability integral (Gauss-Kronrod).
//! boost's gauss_kronrod stops when the estimated error falls below
//! tolerance * L1-norm-of-the-integrand, i.e. the tolerance is RELATIVE to the
//! integrand magnitude, not absolute. Deep OTM the oscillatory integrand largely
//! cancels, so the net result is tiny compared with that L1 norm; a loose tolerance
//! then leaves an absolute error far bigger than the (small) probability and the
//! price is unreliable. A tight relative tolerance with extra refinement depth
//! resolves the cancellation. The integrals are cheap (one analytic contract), so
//! the extra subdivisions cost nothing material.
constexpr double HESTON_CF_LOWER_BOUND = 1e-8; //!< lower limit (integrand regular at 0)
constexpr double HESTON_CF_TOLERANCE = 1e-10;  //!< relative (to integrand L1) convergence tolerance
constexpr unsigned HESTON_CF_MAX_DEPTH = 20;   //!< adaptive-refinement depth

double heston_probability( HestonParams h, int j )
{
    h.j = j;
    //! semi-infinite integral from a small epsilon (integrand is regular at 0),
    //! adaptive Gauss-Kronrod — the same rule family as the GSL qagiu it replaces
    auto integrand = [&h]( double phi )
    { return heston_integrand( phi, &h ); };
    const double result = boost::math::quadrature::gauss_kronrod<double, 15>::integrate(
        integrand, HESTON_CF_LOWER_BOUND, std::numeric_limits<double>::infinity(), HESTON_CF_MAX_DEPTH, HESTON_CF_TOLERANCE );
    return 0.5 + result / M_PI;
}
} // namespace

double Heston_Call_Price( const double Forward,
                          const double Strike,
                          const double TimeToMaturity,
                          const double DiscountFactor,
                          const double V0,
                          const double Kappa,
                          const double Theta,
                          const double Xi,
                          const double Rho,
                          const double JumpIntensity,
                          const double JumpMean,
                          const double JumpVol )
{
    //! degenerate (no vol-of-vol / zero maturity) : flat-vol Black-Scholes. The
    //! characteristic-function integrand divides by xi^2, so Xi must stay > 0;
    //! the Bates jumps require the CF, hence xi > 0 too.
    if ( Xi <= 1e-10 || TimeToMaturity <= 0 || Forward <= 0 || Strike <= 0 )
    {
        return BS_Call_Price( Forward, Strike, TimeToMaturity, sqrt( max( V0, 0.0 ) ), DiscountFactor );
    }
    HestonParams h{ Forward, Strike, TimeToMaturity, V0, Kappa, Theta, Xi, Rho,
                    JumpIntensity, JumpMean, JumpVol, 1 };
    //! same delta-/exercise-probability decomposition as Black-Scholes:
    //! C = df * (F*P1 - K*P2), with P1, P2 the two CF probabilities
    double P1 = heston_probability( h, 1 );
    double P2 = heston_probability( h, 2 );
    return DiscountFactor * ( Forward * P1 - Strike * P2 );
}

double Heston_Put_Price( const double Forward,
                         const double Strike,
                         const double TimeToMaturity,
                         const double DiscountFactor,
                         const double V0,
                         const double Kappa,
                         const double Theta,
                         const double Xi,
                         const double Rho,
                         const double JumpIntensity,
                         const double JumpMean,
                         const double JumpVol )
{
    //! put/call parity (model-independent): P = C - df*(F - K)
    double c = Heston_Call_Price( Forward, Strike, TimeToMaturity, DiscountFactor,
                                  V0, Kappa, Theta, Xi, Rho, JumpIntensity, JumpMean, JumpVol );
    return c - DiscountFactor * ( Forward - Strike );
}

//! ----------------------------------------------------------------------
//! analytic basket (moment-matched) proxy call prices. The moment computation and
//! the moment -> proxy-parameter inversion live in maths.hpp (LN_to_M4 / M*_to_*);
//! these take the resulting proxy parameters and return the closed-form price.
//! ----------------------------------------------------------------------

//! inverse gamma call price: if 1/S ~ Gamma(Alpha, Beta) then S is inverse-gamma;
//! the call is df*(F*G1 - K*G2) with G1, G2 gamma CDFs at 1/K under shifted shapes
//! (the Alpha-1 shift comes from the extra S factor in the first integral).
double IG_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double Alpha,
                      const double Beta )
{
    double G1 = GammaCdf( 1 / Strike, Alpha - 1, Beta );
    double G2 = GammaCdf( 1 / Strike, Alpha, Beta );
    return DiscountFactor * ( Forward * G1 - Strike * G2 );
}

//! lognormal call price = Black-76 with TOTAL variance Var (= sigma^2 T); here Var
//! already aggregates the maturity, so d1/d2 use Var directly (no extra *T).
double LN_Call_Price( const double Forward,
                      const double Strike,
                      const double DiscountFactor,
                      const double /*Mu*/,
                      const double Var )
{
    //! d1 = [ln(F/K) + Var/2] / sqrt(Var), d2 = [ln(F/K) - Var/2] / sqrt(Var)
    double d1 = ( log( Forward / Strike ) + Var / 2 ) / sqrt( Var );
    double d2 = ( log( Forward / Strike ) - Var / 2 ) / sqrt( Var );
    double Nd1 = NormalCdf( d1 );
    double Nd2 = NormalCdf( d2 );
    return DiscountFactor * ( Forward * Nd1 - Strike * Nd2 );
}

//! shifted lognormal call price
double SLN_Call_Price( const double /*Forward*/,
                       const double Strike,
                       const double DiscountFactor,
                       const double Mu,
                       const double Var,
                       const double D )
{
    //! shift already above the strike: the lognormal part (always > 0) keeps the
    //! option in-the-money with certainty, so the price is the discounted forward
    //! value D + E[lognormal] - K (E[lognormal] = exp(Mu + Var/2))
    if ( D >= Strike )
    {
        return DiscountFactor * ( D + exp( Mu + 0.5 * Var ) - Strike );
    }

    else
    {
        //! otherwise a Black-76 on the SHIFTED strike (Strike - D): the lognormal
        //! variable must exceed (Strike - D) for the option to pay
        double v = sqrt( Var );
        double d1 = ( -log( Strike - D ) + Mu + Var ) / v;
        double d2 = d1 - v;
        double Nd1 = NormalCdf( d1 );
        double Nd2 = NormalCdf( d2 );
        return DiscountFactor * ( exp( Mu + 0.5 * Var ) * Nd1 - ( Strike - D ) * Nd2 );
    }
}
