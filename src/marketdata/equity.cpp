#include "thoth.hpp"
#include "equity.hpp"
#include "object_reader.hpp"

//! equity.cpp — Equity implementation: carry/dividend forwards, local vol, MCL nodes.
//! Central theme: one carry yield and one dividend-escrow PV, reused across the ANA
//! forward, the PDE carry and the MCL drift/dividend nodes so the engines agree.

//! constructor — null out the optional curves (repo / continuous dividends); the
//! discrete-dividend pointer defaults to null in the header. All are wired in Configure
Equity::Equity( const string& ObjectName ) : Single( ObjectName, KIND_EQUITY )
{
    _continuous_dividends = nullptr;
    _repo = nullptr;
}

Equity::~Equity() = default;

//! read spot / volatility / currency and the optional dividend & repo schedules. The
//! three dividend/repo blocks are independently optional — each is wired only if the
//! YAML carries the matching reference, so an equity may have any subset of carries
void Equity::Configure( ObjectReader& reader )
{
    SetSpot( reader.Get<double>( "spot" ) );
    SetVolatility( reader.Ref<Volatility>( "volatility" ) );
    SetCurrency( reader.Ref<Currency>( "currency" ) );
    if ( reader.Has<string>( "continuous_dividends" ) )
    {
        SetContinuousDividends( reader.Ref<ContinuousDividendsCurve>( "continuous_dividends" ) );
    }
    if ( reader.Has<string>( "discrete_dividends" ) )
    {
        SetDiscreteDividends( reader.Ref<DiscreteDividends>( "discrete_dividends" ) );
    }
    if ( reader.Has<string>( "repo" ) )
    {
        SetRepo( reader.Ref<RepoCurve>( "repo" ) );
    }
}

//! setter — bind the optional repo curve (non-owning)
void Equity::SetRepo( RepoCurve* Repo )
{
    _repo = Repo;
}

//! setter — bind the optional continuous dividend-yield curve (non-owning)
void Equity::SetContinuousDividends( ContinuousDividendsCurve* ContinuousDividends )
{
    _continuous_dividends = ContinuousDividends;
}

//! setter — bind the optional discrete cash-dividend schedule (non-owning)
void Equity::SetDiscreteDividends( DiscreteDividends* DiscreteDividends )
{
    _discrete_dividends = DiscreteDividends;
}

//! getter — repo curve (may be null)
RepoCurve* Equity::GetRepo()
{
    return _repo;
}

//! getter — continuous dividend-yield curve (may be null)
ContinuousDividendsCurve* Equity::GetContinuousDividends()
{
    return _continuous_dividends;
}

//! escrowed-dividend model: PV of the discrete cash dividends with ex-date in
//! (today, UpTo], discounted on this equity's currency curve.
double Equity::DiscreteDividendsPv( const date& UpTo ) const
{
    if ( !_discrete_dividends )
    {
        return 0;
    }
    const vector<date>& dates = _discrete_dividends->GetDates();
    const vector<double>& amounts = _discrete_dividends->GetAmounts();
    YieldCurve* rate = Asset::_currency->GetRate(); //!< discount on the equity's own curve

    double pv = 0;
    //! sum only the dividends whose ex-date falls strictly after today and on/before
    //! UpTo; each is discounted to today (DF(today) = 1) so pv is an as-of-today PV.
    //! min(dates,amounts) guards a mismatched schedule.
    for ( size_t i = 0; i < dates.size() && i < amounts.size(); i++ )
    {
        if ( dates[i] > Object::_today && dates[i] <= UpTo )
        {
            pv += amounts[i] * rate->GetDiscountFactor( dates[i] );
        }
    }
    return pv;
}

//! escrowed spot for the MCL diffusion (spot minus the PV of dividends up to the
//! last diffusion date)
double Equity::GetDiffusionSpot( const date& LastDate ) const
{
    return _spot - DiscreteDividendsPv( LastDate );
}

//! escrowed-dividend model: future-dividend PV as of AsOf — sum over ex-dates d
//! strictly after AsOf of amount * DF(d) / DF(AsOf), on this equity's currency
//! curve. Identical to the per-date value the MCL DividendNode publishes, so the
//! PDE can add it to the escrowed grid value to recover the observed spot.
double Equity::FutureDividendPv( const date& AsOf ) const
{
    if ( !_discrete_dividends )
    {
        return 0;
    }
    const vector<date>& dates = _discrete_dividends->GetDates();
    const vector<double>& amounts = _discrete_dividends->GetAmounts();
    YieldCurve* rate = Asset::_currency->GetRate();
    const double df_asof = rate->GetDiscountFactor( AsOf );

    double pv = 0;
    //! PV-as-of-today of every dividend strictly after AsOf...
    for ( size_t i = 0; i < dates.size() && i < amounts.size(); i++ )
    {
        if ( dates[i] > AsOf )
        {
            pv += amounts[i] * rate->GetDiscountFactor( dates[i] );
        }
    }
    //! ...then divide by DF(AsOf) to re-express it as a forward value as of AsOf
    //! (DF(d)/DF(AsOf) = the forward discount from d back to AsOf). Guard a zero DF.
    return ( df_asof > 0 ) ? pv / df_asof : pv;
}

//! getter — current spot (delegates to Single, which holds _spot)
double Equity::GetSpot() const
{
    return Single::GetSpot();
}

//! true when the bound vol object is a local-vol surface (Dupire/SABR), which routes
//! pricing through the local-vol grid rather than a constant-vol step
bool Equity::UseLocalVol()
{
    return _volatility->_is_local;
}

//! continuous carry yield (dividend yield + repo spread) subtracted from the rate
//! in the drift. One source for all engines: the ANA forward uses it, the PDE
//! carry subtracts it, and the MCL drift node sums the same div/repo curve nodes.
double Equity::DividendRepoYield( const date& MaturityDate ) const
{
    double y = ( _continuous_dividends ? _continuous_dividends->GetCurveValue( MaturityDate ) : 0 );
    y += ( _repo ? _repo->GetCurveValue( MaturityDate ) : 0 );
    return y;
}

//! escrowed-dividend forward F(T) = (S - PV[discrete divs <= T]) * exp(-q*T) / DF(T):
//! the spot, netted of the PV of cash dividends due before T, grown at the risk-free
//! rate (the 1/DF factor) less the continuous carry q (= dividend yield + repo). The
//! quanto term qto is currently disabled (kept here for reference).
double Equity::GetForward( const date& MaturityDate ) const
{
    double dt = YearFraction( Object::_today, MaturityDate );                   //!< T in years
    double df = Asset::_currency->GetRate()->GetDiscountFactor( MaturityDate ); //!< DF(T)
    //! continuous carry yield: dividend yield + repo (the same quantity the MCL
    //! drift node and the PDE carry subtract, so the three engines agree)
    double div = DividendRepoYield( MaturityDate );

    //! quanto adjustment
    double qto = 0;
    /*
    if ( Asset::_currency != QuantoCurrency )
    {
        //! correlation must be set
        if ( !_correlation )
        {
            ERR( "missing correlation for quanto adjustment computation of " + Object::_name );
        }

        double v = GetImplicitVol( 0, MaturityDate );
        double v_fx = _correlation->GetFxVol( Asset::_currency->GetName(), QuantoCurrency->GetName() );
        double rho  = _correlation->GetValue( Asset::_currency->GetName(), QuantoCurrency->GetName(), Object::_name );
        qto = v_fx * v * rho;
    }
    */

    //! escrowed-dividend model: net the PV of discrete cash dividends due before
    //! maturity off the spot, then grow at the continuous carry
    double spot = _spot - DiscreteDividendsPv( MaturityDate );

    return spot * exp( -dt * ( div + qto ) ) / df;
}

//! Dupire local vol at (Strike, MaturityDate): reconstruct the surface's drift inputs
//! — the zero rate r and the carry yield q (repo + continuous dividends, each optional)
//! — and hand them with spot to the volatility object's local-vol formula
double Equity::GetLocalVolatility( const double Strike,
                                   const date& MaturityDate )
{
    double r = _currency->GetRate()->GetCurveValue( MaturityDate );
    //! repo and continuous dividends are independently optional; sum whichever is present
    double q = 0;
    if ( _repo )
    {
        q += _repo->GetCurveValue( MaturityDate );
    }
    if ( _continuous_dividends )
    {
        q += _continuous_dividends->GetCurveValue( MaturityDate );
    }
    return _volatility->GetLocalVolatility( Strike, MaturityDate, _spot, r, q );
}

//! mcl drift node — sums the term-structured rate node minus the dividend and repo
//! curve nodes (each optional), reproducing (r - q - repo) along the diffusion dates
MonteCarloNode* Equity::GetDriftNode( NodeCollector& NC )
{
    auto init = [&]( DriftNode* D )
    {
        D->SetDomesticRateNode( Asset::_currency->GetRateNode( NC ) );
        D->SetDividendRateNode( ( _continuous_dividends ) ? _continuous_dividends->GetNode( NC ) : nullptr );
        D->SetRepoRateNode( ( _repo ) ? _repo->GetNode( NC ) : nullptr );
    };
    //! the drift depends on the rate (and div/repo, never bumped), so it is
    //! mutualised with the base tree unless the scenario bumps rates (rho)
    if ( NC.Scenario().active() && !NC.Scenario().bumps_rate )
    {
        return NC.GetOrCreateShared<DriftNode>( _name + node_name::DRIFT, init );
    }
    return NC.GetOrCreate<DriftNode>( _name + node_name::DRIFT, init );
}

//! discrete-dividend escrow node: the future-dividend PV at each diffusion date,
//! precomputed from the schedule and this equity's discount curve. Null when the
//! equity carries no discrete dividends.
MonteCarloNode* Equity::GetDividendNode( NodeCollector& NC )
{
    if ( !_discrete_dividends )
    {
        return nullptr;
    }
    auto init = [&]( DividendNode* D )
    {
        const vector<date>& schedule = _discrete_dividends->GetDates();
        const vector<double>& amounts = _discrete_dividends->GetAmounts();
        YieldCurve* rate = Asset::_currency->GetRate();
        //! future-dividend PV as of each diffusion date t: sum over ex-dates after t
        //! of amount * DF(ex) / DF(t) (DF on this equity's curve; DF(today) = 1)
        for ( const date& t : NC.GetDateList() )
        {
            const double df_t = rate->GetDiscountFactor( t );
            double pv = 0;
            for ( size_t j = 0; j < schedule.size() && j < amounts.size(); j++ )
            {
                if ( schedule[j] > t )
                {
                    pv += amounts[j] * rate->GetDiscountFactor( schedule[j] );
                }
            }
            D->PushFuturePv( ( df_t > 0 ) ? pv / df_t : pv );
        }
    };
    //! depends on the discount curve (rho) only: shared with the base tree unless
    //! the scenario bumps rates
    if ( NC.Scenario().active() && !NC.Scenario().bumps_rate )
    {
        return NC.GetOrCreateShared<DividendNode>( _name + node_name::DIVIDEND, init );
    }
    return NC.GetOrCreate<DividendNode>( _name + node_name::DIVIDEND, init );
}