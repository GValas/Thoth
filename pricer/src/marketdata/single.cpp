#include "thoth.hpp"
#include "single.hpp"
#include "correlation.hpp"
#include "currency.hpp"
#include "yield_curve.hpp"
#include "rng.hpp"

//! single.cpp — Single implementation: spot/vol accessors, the spot-diffusion node
//! graph (GBM / Heston / local-vol grid), the LSV leverage calibration and the
//! contract-diffusion (Mono) role.

//! constructor — null the vol pointer; spot defaults to 0 in the header and both are
//! wired by the concrete name's Configure
Single::Single( const string& ObjectName,
                const string& ObjectKind ) : Underlying( ObjectName, ObjectKind )
{
    _volatility = nullptr;
}

//! destructor
Single::~Single() = default;

//! setter — bind the volatility surface by address (shared book object, not owned)
void Single::SetVolatility( Volatility* Volatility )
{
    _volatility = Volatility;
}

//! setter — date the vol surface first (so a term-structured surface is aligned),
//! then push the date up the Asset chain (which dates the currency/curve)
void Single::SetToday( const date& Today )
{
    _volatility->SetToday( Today );
    Asset::SetToday( Today );
}

//! setter — the spot price
void Single::SetSpot( double Spot )
{
    _spot = Spot;
}

//! getter — the bound volatility surface
Volatility* Single::GetVolatility() const
{
    return _volatility;
}

//! getter — current spot price
double Single::GetSpot() const
{
    return _spot;
}

//! implicit vol
double Single::GetImplicitVol( const double Strike,
                               const date& MaturityDate )
{
    //! supply the underlying's forward so a forward-measure surface (SABR) is
    //! evaluated correctly; flat surfaces ignore it
    return _volatility->GetImplicitVol( Strike, GetForward( MaturityDate ), MaturityDate );
}

//! mcl nodes
MonteCarloNode* Single::GetNode( NodeCollector& NC )

{
    //! stochastic vol (Heston): a dedicated variance + spot diffusion (Andersen
    //! QE) instead of the constant-vol GBM step. Reuses the shared node_name::WHITE_NOISE
    //! for the spot's independent Gaussian and node_name::VOL_WHITE_NOISE for the variance.
    if ( _volatility->IsStochastic() )
    {
        const StochasticVolParams h = _volatility->StochasticParams();
        MonteCarloNode* var = NC.GetOrCreate<HestonVarianceNode>(
            _name + node_name::VARIANCE,
            [&]( HestonVarianceNode* V )
            {
                V->SetParameters( h.v0, h.kappa, h.theta, h.xi );
                V->SetNoiseNode( NC.GetNode( _name + node_name::VOL_WHITE_NOISE ) );
            } );
        return NC.GetOrCreate<HestonSpotNode>(
            _name + node_name::SPOT,
            [&]( HestonSpotNode* S )
            {
                S->SetParameters( h.kappa, h.theta, h.xi, h.rho );
                S->SetVarianceNode( var );
                S->SetDriftNode( GetDriftNode( NC ) );
                S->SetNoiseNode( NC.GetNode( _name + node_name::WHITE_NOISE ) );
                //! Bates : wire the jump source if this stochastic vol carries jumps
                if ( h.has_jumps() )
                {
                    S->SetJumpNode( NC.GetNode( _name + node_name::JUMP_NOISE ) );
                }
                //! LSV : multiply the spot diffusion by the calibrated leverage
                //! L(S,t) (a per-date grid read at the previous spot, like the
                //! Dupire local-vol node). Calibrated per scenario, so a vega /
                //! vega_<param> bump recalibrates against the bumped surface.
                if ( _volatility->IsLsv() )
                {
                    S->SetLeverageNode( BuildLeverageNode( NC, S ) );
                }
                S->SetSpot( GetDiffusionSpot( NC.GetDateList().back() ) );
            } );
    }

    return NC.GetOrCreate<SpotDiffusionNode>(
        _name + node_name::SPOT,
        [&]( SpotDiffusionNode* S )
        {
            S->SetBrownianNode( NC.GetNode( _name + node_name::BROWNIAN ) );
            S->SetDriftNode( GetDriftNode( NC ) );
            //! discrete dividends (if any) are escrowed inside the diffusion via the
            //! dividend node, so the diffusion starts from the full spot
            S->SetSpot( _spot );
            S->SetDividendNode( GetDividendNode( NC ) );
            //! local-vol surface (e.g. SABR): sample the Dupire surface onto a
            //! per-date log-spot grid the diffusion reads along its own path.
            //! Flat surface (bs): one constant-vol node.
            if ( _volatility->_is_local )
            {
                LocalVolatilityNode* lv = BuildLocalVolNode( NC, S );
                S->SetLocalVolNode( lv );
                //! always refine the local-vol diffusion with the Milstein step
                //! (its correction is a no-op for constant vol, so it is only ever
                //! applied here, where the vol genuinely depends on the spot)
                S->EnableMilstein( lv );
            }
            else
            {
                S->SetLocalVolNode( GetVolNode( NC ) );
            }
        } );
}

MonteCarloNode* Single::GetVolNode( NodeCollector& NC )
{
    //! A local surface (SABR) has no single scalar "vol node" — the spot diffusion
    //! reads its full grid (see BuildLocalVolNode). The remaining scalar-vol callers
    //! (the quanto drift correction, the composite vol/correl nodes) only need a
    //! representative level, so give them a constant ATM vol at the last diffusion
    //! date — the same ATM vol the ANA/PDE quanto correction uses. Rebuilt per Greek
    //! scenario (suffixed GetOrCreate) so a vega bump re-samples it.
    if ( _volatility->_is_local )
    {
        const vector<date>& dates = NC.GetDateList();
        double atm = GetImplicitVol( 0, dates.back() );
        return NC.GetOrCreate<ConstantNode>( _name + node_name::ATM_VOL,
                                             [&]( ConstantNode* C )
                                             { C->SetConstantValue( atm ); } );
    }
    return _volatility->GetNode( NC );
}

//! half-width of the log-spot grid in ATM-vol standard deviations: wide enough that
//! almost every diffused path lands inside it (paths beyond clamp to the boundary
//! local vol). Generous vs the PDE's factor because MC paths reach further tails.
static constexpr double LOCAL_VOL_GRID_SIGMA_FACTOR = 8.0;
//! log-spot grid points per diffusion date (odd -> a node sits on the spot)
static constexpr int LOCAL_VOL_GRID_POINTS = 201;

LocalVolatilityNode* Single::BuildLocalVolNode( NodeCollector& NC, MonteCarloNode* SpotNode )
{
    return static_cast<LocalVolatilityNode*>( NC.GetOrCreate<LocalVolatilityNode>(
        _name + node_name::LOCAL_VOL,
        [&]( LocalVolatilityNode* L )
        {
            L->SetSpotNode( SpotNode );

            const vector<date>& dates = NC.GetDateList();
            const date today = dates.front();
            const double ln_spot = log( _spot );

            //! one Dupire-sampled log-spot grid per diffusion date
            for ( size_t k = 0; k < dates.size(); k++ )
            {
                double T = YearFraction( today, dates[k] );
                if ( T <= 0 )
                {
                    T = 1.0 / NB_OF_DAYS_A_YEAR; //!< today: degenerate, never diffused
                }
                double v_atm = GetImplicitVol( 0, dates[k] ); //!< ATM vol, grid sizing only
                double half_width = LOCAL_VOL_GRID_SIGMA_FACTOR * v_atm * sqrt( T );
                double ln_step = 2.0 * half_width / ( LOCAL_VOL_GRID_POINTS - 1 );
                if ( ln_step <= 0 )
                {
                    ln_step = 1e-4; //!< guard for v_atm ~ 0
                }
                //! signed log-spot index of the grid's first point, so grid point i
                //! sits at log-spot = (offset + i) * ln_step, centred on the spot
                long offset = (long)floor( ( ln_spot - half_width ) / ln_step );

                la_vector* vol = la_vector_alloc( LOCAL_VOL_GRID_POINTS );
                for ( int i = 0; i < LOCAL_VOL_GRID_POINTS; i++ )
                {
                    double s_grid = exp( ( offset + i ) * ln_step );
                    la_vector_set( vol, i, GetLocalVolatility( s_grid, dates[k] ) );
                }
                L->PushLnStep( ln_step );
                L->PushOffset( offset );
                L->PushVolVector( vol );
            }
        } ) );
}

//! ----------------------------------------------------------------------
//! LSV leverage calibration (binned particle method)
//! ----------------------------------------------------------------------

//! particle count of the calibration ensemble: enough for a stable binned
//! conditional expectation E[v|S] without dominating the pricing cost
static constexpr int LSV_CALIB_PATHS = 16384;
//! log-spot bins for the conditional expectation, and the minimum population
//! below which a bin inherits its nearest populated neighbour's value
static constexpr int LSV_CALIB_BINS = 30;
static constexpr int LSV_CALIB_MIN_BIN_COUNT = 20;
//! floor on the conditional variance ((0.1% vol)^2) and clamp on the leverage:
//! the Dupire target has no upper cap in the far wings (Hagan butterfly
//! arbitrage can inflate it), so the leverage is kept in a sane band rather
//! than letting a degenerate wing node poison the diffusion
static constexpr double LSV_MIN_COND_VARIANCE = 1e-6;
static constexpr double LSV_LEVERAGE_MIN = 0.05;
static constexpr double LSV_LEVERAGE_MAX = 10.0;
//! fixed calibration seed: deterministic reprices, and common random numbers
//! across Greek-bump recalibrations (the bump difference is not polluted by a
//! resampled calibration noise)
static constexpr uint64_t LSV_CALIB_SEED = 20260708;

namespace
{
//! binned conditional expectation E[v | ln S] over a particle ensemble: equal-width
//! log-spot bins between the ensemble extremes; a thin bin (fewer than
//! LSV_CALIB_MIN_BIN_COUNT particles) inherits the nearest populated bin so the
//! wings never divide by a noisy near-empty average. Lookup is piecewise-linear
//! between bin centres, clamped at the ends.
class ConditionalVariance
{
  private:
    double _x0 = 0, _dx = 1; //!< first bin centre and bin width
    vector<double> _mean;    //!< per-bin conditional mean of v (all populated after Build)
    double _fallback = 0;    //!< ensemble mean (degenerate range, e.g. the first date)

  public:
    ConditionalVariance( const vector<double>& LnSpot, const vector<double>& Var )
    {
        _fallback = 0;
        for ( double v : Var )
        {
            _fallback += v;
        }
        _fallback /= (double)Var.size();

        const auto [mn, mx] = std::minmax_element( LnSpot.begin(), LnSpot.end() );
        if ( *mx - *mn < 1e-10 )
        {
            return; //!< all particles at one spot (first steps): E[v|S] = mean(v)
        }

        const double width = ( *mx - *mn ) / LSV_CALIB_BINS;
        vector<double> sum( LSV_CALIB_BINS, 0.0 );
        vector<int> count( LSV_CALIB_BINS, 0 );
        for ( size_t p = 0; p < LnSpot.size(); p++ )
        {
            int b = (int)( ( LnSpot[p] - *mn ) / width );
            b = std::clamp( b, 0, LSV_CALIB_BINS - 1 );
            sum[b] += Var[p];
            count[b]++;
        }
        _mean.assign( LSV_CALIB_BINS, _fallback );
        //! populated bins first, then fill each thin bin from its nearest populated one
        for ( int b = 0; b < LSV_CALIB_BINS; b++ )
        {
            if ( count[b] >= LSV_CALIB_MIN_BIN_COUNT )
            {
                _mean[b] = sum[b] / count[b];
            }
        }
        for ( int b = 0; b < LSV_CALIB_BINS; b++ )
        {
            if ( count[b] >= LSV_CALIB_MIN_BIN_COUNT )
            {
                continue;
            }
            for ( int d = 1; d < LSV_CALIB_BINS; d++ )
            {
                if ( b - d >= 0 && count[b - d] >= LSV_CALIB_MIN_BIN_COUNT )
                {
                    _mean[b] = sum[b - d] / count[b - d];
                    break;
                }
                if ( b + d < LSV_CALIB_BINS && count[b + d] >= LSV_CALIB_MIN_BIN_COUNT )
                {
                    _mean[b] = sum[b + d] / count[b + d];
                    break;
                }
            }
        }
        _x0 = *mn + 0.5 * width;
        _dx = width;
    }

    //! E[v | ln S = X]: linear between bin centres, clamped at the extreme bins
    double operator()( double X ) const
    {
        if ( _mean.empty() )
        {
            return _fallback;
        }
        const double f = ( X - _x0 ) / _dx;
        const double last = (double)( _mean.size() - 1 );
        if ( f <= 0 )
        {
            return _mean.front();
        }
        if ( f >= last )
        {
            return _mean.back();
        }
        const size_t i = (size_t)f;
        const double w = f - (double)i;
        return ( 1.0 - w ) * _mean[i] + w * _mean[i + 1];
    }
};

//! one Andersen-QE CIR variance step (the exact HestonVarianceNode::ComputeValue
//! scheme, kept in sync so the calibration ensemble follows the pricing diffusion)
double QeVarianceStep( double V, double Dt, double Z, const StochasticVolParams& H )
{
    const double e = exp( -H.kappa * Dt );
    const double m = H.theta + ( V - H.theta ) * e;
    const double s2 = V * H.xi * H.xi * e / H.kappa * ( 1 - e ) +
                      H.theta * H.xi * H.xi / ( 2 * H.kappa ) * ( 1 - e ) * ( 1 - e );
    const double psi = ( m > 0 ) ? s2 / ( m * m ) : 0;
    if ( psi <= 1.5 )
    {
        const double c = 2.0 / psi;
        const double b2 = c - 1.0 + sqrt( c ) * sqrt( c - 1.0 );
        const double a = m / ( 1.0 + b2 );
        const double bz = sqrt( b2 ) + Z;
        return a * bz * bz;
    }
    const double p = ( psi - 1.0 ) / ( psi + 1.0 );
    const double beta = ( 1.0 - p ) / m;
    const double u = 0.5 * erfc( -Z / sqrt( 2.0 ) );
    return ( u <= p ) ? 0.0 : log( ( 1.0 - p ) / ( 1.0 - u ) ) / beta;
}

//! one leveraged Andersen log-spot step (the exact HestonSpotNode::ComputeValue
//! scheme with its leverage folding, kept in sync for the same reason)
double LeveragedLogSpotStep( double Vp, double Vc, double Dt, double Carry, double L,
                             double Z, const StochasticVolParams& H )
{
    if ( H.xi > 1e-12 )
    {
        const double g = 0.5;
        const double k0 = -L * H.rho * H.kappa * H.theta / H.xi * Dt;
        const double a = g * Dt * ( L * H.kappa * H.rho / H.xi - 0.5 * L * L );
        const double k1 = a - L * H.rho / H.xi;
        const double k2 = a + L * H.rho / H.xi;
        const double k3 = g * Dt * ( 1.0 - H.rho * H.rho ) * L * L;
        double var = k3 * ( Vp + Vc );
        if ( var < 0 )
        {
            var = 0;
        }
        return Carry + k0 + k1 * Vp + k2 * Vc + sqrt( var ) * Z;
    }
    return Carry - 0.5 * L * L * Vp * Dt + L * sqrt( Vp * Dt ) * Z;
}
} // namespace

//! LSV leverage calibration by the binned particle method (a bin-kernel variant of
//! Guyon & Henry-Labordere): simulate the leveraged diffusion forward along Dates
//! and, before each step into date k, build leverage layer k from the Dupire
//! matching condition
//!   L^2(s, t_k) * E[v_{k-1} | S_{k-1} = s] = sigma_dupire^2(s, t_k)
//! on the target implied surface. Layer k is what the step INTO date k uses — both
//! here and in the engine (the leverage node reads layer k at the previous spot),
//! so the pricing diffusion reproduces the calibrated ensemble exactly (up to the
//! path count). Layer 0 (today, never diffused) uses E = v0.
LeverageSurface Single::CalibrateLeverage( const vector<date>& Dates )
{
    const StochasticVolParams h = _volatility->StochasticParams();
    const date today = Dates.front();
    const double s0 = GetDiffusionSpot( Dates.back() );

    //! cumulative net carry (r - q - repo)(t) * t to a date — the same quantity the
    //! MCL drift node publishes, so the calibration drift matches the engine's
    auto cum_carry = [&]( const date& d )
    {
        const double r = GetCurrency()->GetRate()->GetCurveValue( d );
        return ( r - DividendRepoYield( d ) ) * YearFraction( today, d );
    };

    //! the particle ensemble, seeded at the (escrowed) diffusion spot and v0
    Rng rng( LSV_CALIB_SEED );
    vector<double> spot( LSV_CALIB_PATHS, s0 );
    vector<double> var( LSV_CALIB_PATHS, h.v0 );
    vector<double> ln_spot( LSV_CALIB_PATHS, log( s0 ) );

    LeverageSurface lev;

    for ( size_t k = 0; k < Dates.size(); k++ )
    {
        //! conditional variance of the CURRENT ensemble (date k-1; v0 flat at k=0)
        const ConditionalVariance cond_var( ln_spot, var );

        //! layer k on the local-vol grid convention (same sizing as BuildLocalVolNode)
        double T = YearFraction( today, Dates[k] );
        const double t_layer = T; //!< layer time for the (S,t) lookups
        if ( T <= 0 )
        {
            T = 1.0 / NB_OF_DAYS_A_YEAR; //!< today: degenerate, never diffused
        }
        const double v_atm = GetImplicitVol( 0, Dates[k] ); //!< ATM vol, grid sizing only
        const double half_width = LOCAL_VOL_GRID_SIGMA_FACTOR * v_atm * sqrt( T );
        double ln_step = 2.0 * half_width / ( LOCAL_VOL_GRID_POINTS - 1 );
        if ( ln_step <= 0 )
        {
            ln_step = 1e-4; //!< guard for v_atm ~ 0
        }
        const long offset = (long)floor( ( log( s0 ) - half_width ) / ln_step );

        vector<double> layer( LOCAL_VOL_GRID_POINTS );
        for ( int i = 0; i < LOCAL_VOL_GRID_POINTS; i++ )
        {
            const double x = ( offset + i ) * ln_step; //!< grid log-spot
            const double sig = GetLocalVolatility( exp( x ), Dates[k] );
            const double ev = std::max( cond_var( x ), LSV_MIN_COND_VARIANCE );
            layer[i] = std::clamp( sig / sqrt( ev ), LSV_LEVERAGE_MIN, LSV_LEVERAGE_MAX );
        }
        lev.PushLayer( t_layer, ln_step, offset, std::move( layer ) );

        //! advance the ensemble into date k with the layer just built (except k=0,
        //! where the ensemble already sits at today)
        if ( k == 0 )
        {
            continue;
        }
        const double dt = YearFraction( Dates[k - 1], Dates[k] );
        const double carry = cum_carry( Dates[k] ) - cum_carry( Dates[k - 1] );
        for ( int p = 0; p < LSV_CALIB_PATHS; p++ )
        {
            const double lp = lev.LayerAt( k, spot[p] );
            const double vp = var[p];
            const double vc = QeVarianceStep( vp, dt, rng.Gaussian(), h );
            const double step = LeveragedLogSpotStep( vp, vc, dt, carry, lp, rng.Gaussian(), h );
            var[p] = vc;
            spot[p] *= exp( step );
            ln_spot[p] += step;
        }
    }
    return lev;
}

//! MCL leverage node: a per-date leverage grid read at the previous spot (the
//! LocalVolatilityNode mechanics), filled from a fresh calibration on the
//! collector's diffusion dates. Plain GetOrCreate, so every Greek scenario that
//! bumps the vol or a model parameter rebuilds (and recalibrates) it.
LocalVolatilityNode* Single::BuildLeverageNode( NodeCollector& NC, MonteCarloNode* SpotNode )
{
    return static_cast<LocalVolatilityNode*>( NC.GetOrCreate<LocalVolatilityNode>(
        _name + node_name::LEVERAGE,
        [&]( LocalVolatilityNode* L )
        {
            L->SetSpotNode( SpotNode );
            const LeverageSurface lev = CalibrateLeverage( NC.GetDateList() );
            for ( size_t k = 0; k < lev.Layers(); k++ )
            {
                const vector<double>& layer = lev.Levels( k );
                la_vector* v = la_vector_alloc( (int)layer.size() );
                for ( size_t i = 0; i < layer.size(); i++ )
                {
                    la_vector_set( v, (int)i, layer[i] );
                }
                L->PushLnStep( lev.LnStep( k ) );
                L->PushOffset( lev.Offset( k ) );
                L->PushVolVector( v );
            }
        } ) );
}

//! --- Underlying (contract-diffusion) role, formerly the Mono adapter ---

double Single::GetForward( const date& MaturityDate,
                           Currency* QuantoCurrency )
{
    double f = GetForward( MaturityDate );

    //! quanto drift correction: when the payoff is settled in a currency other than
    //! the underlying's, the asset's forward in the payoff-currency measure is
    //! F *= exp( -rho(S,FX) * sigma_S * sigma_X * t ). This mirrors the MCL
    //! QuantoAdjustmentNode (exp(-v*w*c*dt) on the spot) so ANA/PDE/MCL agree. rho is
    //! the signed FX-underlying correlation (GetValue already carries the sign), so no
    //! extra negation here. The correction uses the ATM implied vol (GetImplicitVol at
    //! strike 0); under a flat BS surface this is exact, but for a local/stochastic-vol
    //! surface it is an approximation of the instantaneous-vol drift term. Currency
    //! identity is by pointer: all Currency objects are singletons in the book graph.
    if ( QuantoCurrency && QuantoCurrency != _currency )
    {
        if ( !_correlation )
        {
            ERR( "missing correlation for quanto adjustment of " + _name );
        }
        double dt = YearFraction( _today, MaturityDate );
        double v_s = GetImplicitVol( 0, MaturityDate );
        double v_fx = _correlation->GetFxVol( _currency->GetName(), QuantoCurrency->GetName() );
        double rho = _correlation->GetValue( _currency->GetName(), QuantoCurrency->GetName(), _name );
        f *= exp( -rho * v_s * v_fx * dt );
    }
    return f;
}

//! a single name spans exactly itself (the set holds mutable handles the pricing
//! mutates — vol/rate bumps — hence the const_cast off this const getter)
SingleSet Single::GetSingleSet() const
{
    SingleSet s;
    s.insert( const_cast<Single*>( this ) );
    return s;
}

CurrencySet Single::GetCurrencySet() const
{
    CurrencySet s;
    s.insert( GetCurrency() );
    return s;
}

MonteCarloNode* Single::GetCorrelNode( NodeCollector& NC,
                                       const string& UnderlyingCurrency,
                                       const string& BaseCurrency )
{
    return _correlation->GetCorrelNode( NC, UnderlyingCurrency, BaseCurrency, _name );
}