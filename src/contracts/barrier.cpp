#include "thoth.hpp"
#include "barrier.hpp"
#include "distributions.hpp"

Barrier::Barrier( const string& ObjectName ) : Contract( ObjectName, KIND_BARRIER )
{
}

Barrier::~Barrier() = default;

//! getter
date Barrier::GetMaturityDate()
{
    return _maturity_date;
}

//! monitoring schedule for a discrete barrier: today + k*period (k>=1) up to,
//! and always including, maturity. Empty period -> just maturity.
set<date> Barrier::GetMonitoringDates()
{
    set<date> s;
    if ( _monitoring_period_days > 0 )
    {
        for ( date d = _today + days( _monitoring_period_days );
              d < _maturity_date;
              d += days( _monitoring_period_days ) )
        {
            s.insert( d );
        }
    }
    s.insert( _maturity_date );
    return s;
}

set<date> Barrier::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    //! a discrete barrier must be diffused at every monitoring date
    if ( IsDiscrete() )
    {
        set<date> m = GetMonitoringDates();
        s.insert( m.begin(), m.end() );
    }
    return s;
}

set<date> Barrier::GetFlowDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

set<date> Barrier::GetAmericanExerciseDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! terminal payoff is the vanilla one; the knock-out is enforced by the grid
//! boundary (continuous monitoring), so no special handling is needed here.
double Barrier::Intrinsic( const double spot )
{
    return payoff_vanilla( spot, _strike, _type, false, 0, true, 0 );
}

//! the PDE prices a single barrier on an equity-like underlying: continuous
//! monitoring by a Dirichlet boundary, discrete monitoring by zeroing the
//! knocked region at the scheduled dates (knock-in by in/out parity).
bool Barrier::PDE_HasSolution()
{
    return _underlying->IsGriddable();
}

bool Barrier::PDE_IsBarrier()
{
    return true;
}

bool Barrier::PDE_IsKnockIn()
{
    return IsKnockIn( _barrier_type );
}

bool Barrier::PDE_IsUpBarrier()
{
    return IsUpBarrier( _barrier_type );
}

double Barrier::PDE_BarrierLevel()
{
    return PDE_IsUpBarrier() ? _barrier_up_level : _barrier_down_level;
}

//! a closed-form (Reiner-Rubinstein) solution exists for a single, continuously
//! monitored barrier on an equity-like underlying.
bool Barrier::ANA_HasSolution()
{
    return ( _barrier_monitoring_type == BarrierMonitoring::Continuous ) &&
           _underlying->IsGriddable();
}

//! Reiner-Rubinstein closed-form barrier price for a single spot value.
//! Knock-out values are obtained from the knock-in ones through in/out parity
//! (vanilla = knock_in + knock_out), so a single set of formulas is needed.
double Barrier::ANA_BarrierPrice( double S,
                                  double r,
                                  double b,
                                  double v,
                                  double t,
                                  double df )
{
    bool is_call = ( _type == OptionType::Call );
    bool is_down = ( _barrier_type == BarrierType::DownAndOut ||
                     _barrier_type == BarrierType::DownAndIn );
    bool is_in = IsKnockIn( _barrier_type );
    double H = is_down ? _barrier_down_level : _barrier_up_level;
    double K = _strike;

    //! vanilla reference (consistent with Vanilla::ANA_EvalPrice)
    double fwd = S * exp( b * t );
    double vanilla = is_call ? BS_Call_Price( fwd, K, t, v, df )
                             : BS_Put_Price( fwd, K, t, v, df );

    //! degenerate inputs : fall back to the vanilla / parity value
    if ( v <= 0 || t <= 0 || S <= 0 || H <= 0 )
    {
        return is_in ? 0.0 : vanilla;
    }

    //! barrier already breached at valuation
    bool breached = is_down ? ( S <= H ) : ( S >= H );
    if ( breached )
    {
        return is_in ? vanilla : 0.0;
    }

    //! Reiner-Rubinstein building blocks (Haug, "Option Pricing Formulas")
    double phi = is_call ? 1.0 : -1.0;
    double eta = is_down ? 1.0 : -1.0;
    double sqt = v * sqrt( t );
    double mu = ( b - 0.5 * v * v ) / ( v * v );

    double x1 = log( S / K ) / sqt + ( 1 + mu ) * sqt;
    double x2 = log( S / H ) / sqt + ( 1 + mu ) * sqt;
    double y1 = log( H * H / ( S * K ) ) / sqt + ( 1 + mu ) * sqt;
    double y2 = log( H / S ) / sqt + ( 1 + mu ) * sqt;

    double Sbr = S * exp( ( b - r ) * t ); //!< S e^{(b-r)t} = fwd * df
    double Kdf = K * df;
    double pHS_p1 = pow( H / S, 2 * ( mu + 1 ) );
    double pHS_m = pow( H / S, 2 * mu );

    auto A = [&]()
    { return phi * Sbr * NormalCdf( phi * x1 ) - phi * Kdf * NormalCdf( phi * ( x1 - sqt ) ); };
    auto B = [&]()
    { return phi * Sbr * NormalCdf( phi * x2 ) - phi * Kdf * NormalCdf( phi * ( x2 - sqt ) ); };
    auto C = [&]()
    { return phi * Sbr * pHS_p1 * NormalCdf( eta * y1 ) - phi * Kdf * pHS_m * NormalCdf( eta * ( y1 - sqt ) ); };
    auto D = [&]()
    { return phi * Sbr * pHS_p1 * NormalCdf( eta * y2 ) - phi * Kdf * pHS_m * NormalCdf( eta * ( y2 - sqt ) ); };

    //! knock-in value (rebate = 0)
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

    return is_in ? knock_in : vanilla - knock_in;
}

//! closed-form barrier pricing (premium + finite-difference spot greeks)
void Barrier::ANA_EvalPrice()
{
    //! market data (same conventions as Vanilla::ANA_EvalPrice)
    double t = YearFraction( _today, _maturity_date );
    double df = _premium_currency->GetRate()->GetDiscountFactor( _maturity_date );
    double f = _underlying->GetForward( _maturity_date, _premium_currency );
    double s = _underlying->GetSpot();
    double v = _underlying->GetImplicitVol( _strike, _maturity_date );

    //! risk-free rate and cost-of-carry implied by the discount factor & forward
    double r = ( t > 0 ) ? -log( df ) / t : 0.0;
    double b = ( t > 0 && s > 0 ) ? log( f / s ) / t : 0.0;

    //! premium
    _valuation.premium = ANA_BarrierPrice( s, r, b, v, t, df );

    //! delta & gamma by central finite difference on the spot
    double s_up = s * ( 1 + GREEK_SPOT_BUMP / 2 );
    double s_dw = s * ( 1 - GREEK_SPOT_BUMP / 2 );
    double p_up = ANA_BarrierPrice( s_up, r, b, v, t, df );
    double p_dw = ANA_BarrierPrice( s_dw, r, b, v, t, df );
    _valuation.delta = ( p_up - p_dw ) / ( s * GREEK_SPOT_BUMP );
    _valuation.gamma = ( p_up + p_dw - 2 * _valuation.premium ) /
                       ( s * GREEK_SPOT_BUMP / 2 * s * GREEK_SPOT_BUMP / 2 );
}

MonteCarloNode* Barrier::GetFlowNode( NodeCollector& NC,
                                      const date& /*AsOfDate*/ )
{

    return NC.GetOrCreate<BarrierFlowNode>(
        _name + "#flow",
        [&]( BarrierFlowNode* C )
        {
            C->SetFloor( 0 );
            C->SetType( _type );
            C->SetStrike( _strike );
            C->SetSpotNode( GetUnderlyingNode( NC ) );
            C->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );

            bool is_up = PDE_IsUpBarrier();
            double H = PDE_BarrierLevel();

            vector<size_t> monitor;
            if ( IsDiscrete() )
            {
                //! monitor exactly at the scheduled dates, no continuity correction
                for ( const date& d : GetMonitoringDates() )
                {
                    monitor.push_back( NC.GetDateIndex( d ) );
                }
            }
            else
            {
                //! continuous: monitor every diffusion date up to maturity, with a
                //! Broadie-Glasserman-Kou continuity correction (the discretely
                //! monitored barrier misses crossings between dates, so the level
                //! is moved towards the spot by exp(-/+ 0.5826 * vol * sqrt(dt))).
                monitor = NC.DiffusionIndicesUpTo( _maturity_date );
                double t = YearFraction( _today, _maturity_date );
                size_t steps = ( monitor.size() > 1 ) ? monitor.size() - 1 : 1;
                double dt = t / (double)steps;
                double vol = _underlying->GetImplicitVol( _strike, _maturity_date );
                const double beta = 0.5826;
                H *= exp( ( is_up ? -1.0 : 1.0 ) * beta * vol * sqrt( dt ) );
            }

            C->SetBarrierLevel( H );
            C->SetIsUp( is_up );
            C->SetIsIn( PDE_IsKnockIn() );
            C->SetMonitorIndexList( monitor );
        } );
}