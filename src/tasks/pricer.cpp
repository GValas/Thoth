#include "thoth.hpp"
#include "pricer.hpp"
#include "heston_volatility.hpp"
#include "cancellation.hpp"
#include "progress_bar.hpp"

//! constructor (members are initialised in-class)
Pricer::Pricer( const string& ObjectName,
                YamlConfig& YamlConfig ) : Task( ObjectName, YamlConfig, KIND_PRICER ) {}

//! destructor
Pricer::~Pricer() = default;

// setter
void Pricer::SetBook( Book& Book )
{
    _book = &Book;
}

// setter
void Pricer::SetCorrelation( Correlation* Correlation )
{
    _correlation = Correlation;
}

// setter
void Pricer::SetConfiguration( PricerConfiguration& Configuration )
{
    _configuration = &Configuration;
}

// setter
void Pricer::SetIndicatorRequestList( const vector<string>& IndicatorRequestList )
{
    _indicator_request_list = IndicatorRequestList;

    //! derive the per-greek request flags from the requested indicator names
    //! (these gate the PDE grid solve; previously left uninitialised)
    auto requested = [&]( const string& name )
    {
        return std::find( _indicator_request_list.begin(), _indicator_request_list.end(), name ) !=
               _indicator_request_list.end();
    };
    _request_premium = requested( "premium" );
    _request_delta = requested( "delta" );
    _request_gamma = requested( "gamma" );
    _request_vega = requested( "vega" );
    _request_rho = requested( "rho" );
    _request_theta = requested( "theta" );

    //! model-parameter Greeks: any "vega_<param>" indicator (vega_alpha, vega_v0,
    //! vega_jump_vol, ...) requests a bump of that named parameter. "vega" alone is
    //! the standard parallel-vol vega handled above, not a parameter bump.
    _param_greek_list.clear();
    for ( const string& ind : _indicator_request_list )
    {
        if ( ind.rfind( "vega_", 0 ) == 0 && ind.size() > 5 )
        {
            _param_greek_list.push_back( ind.substr( 5 ) );
        }
    }
}

// setter
void Pricer::SetCurrency( Currency& Currency )
{
    _currency = &Currency;
}

//! getter
date Pricer::GetToday() const
{
    return _today;
}

void Pricer::Execute()
{
    //! validate the book / configuration once for the chosen engine
    PreCheck();

    double t0 = WallClockSeconds();

    //! base scenario : price the whole book at the quoted market. For the
    //! per-contract engines (PDE, ANA) PriceBook already folds in each
    //! contract's Greeks inside its contract loop, so ComputeGreeks is skipped.
    PriceBook();
    _premium = _book->GetPremium();

    //! optional Greeks by book-level bump-and-revalue (MCL); restores the base book
    const bool greeks = _request_delta || _request_gamma || _request_vega ||
                        _request_rho || _request_theta;
    if ( greeks && !GreeksPerContract() )
    {
        ComputeGreeks();
    }

    //! model-parameter Greeks (vega_<param>) — book-level bump-and-revalue, shared
    //! by every engine (the standard per-contract Greeks above stay engine-specific)
    if ( !_param_greek_list.empty() )
    {
        ComputeParamGreeks();
    }

    _exec_time = ExecTime( t0 );
}

//! model-parameter Greeks: for each requested vega_<param>, bump that parameter on
//! every underlying whose vol surface exposes it, reprice the whole book, and take
//! the one-sided finite difference (per unit of the parameter). Skips a parameter
//! no surface in the book has (e.g. vega_alpha on a Heston book). Book-level, so it
//! works the same for MCL (re-diffuses with common random numbers) and ANA / PDE.
void Pricer::ComputeParamGreeks()
{
    _quiet_pricing = true;
    const double p0 = _premium;

    //! snapshot the surfaces once: PriceBook -> InitPricing rebuilds _single_set,
    //! but the Single / Volatility objects themselves persist across re-prices.
    const vector<Single*> singles( _single_set.begin(), _single_set.end() );

    for ( const string& param : _param_greek_list )
    {
        vector<Volatility*> vols;
        for ( Single* s : singles )
        {
            Volatility* v = s->GetVolatility();
            if ( v->HasParam( param ) )
            {
                vols.push_back( v );
            }
        }
        if ( vols.empty() )
        {
            continue; //!< no surface in this book has the parameter — skip silently
        }

        for ( Volatility* v : vols )
        {
            v->SetParamShift( param, GREEK_PARAM_BUMP );
        }
        PriceBook();
        const double pu = _book->GetPremium();
        for ( Volatility* v : vols )
        {
            v->SetParamShift( param, 0 ); //!< restore
        }
        _param_greeks[param] = ( pu - p0 ) / GREEK_PARAM_BUMP;
    }

    //! restore the book to the base scenario so the premium output is unbumped
    PriceBook();
    _premium = _book->GetPremium();
    _quiet_pricing = false;
}

//! parallel vol shift applied to every underlying's volatility
void Pricer::ApplyVolShift( double Shift )
{
    for ( Single* s : _single_set )
    {
        s->GetVolatility()->SetVolShift( Shift );
    }
}

//! parallel shift applied to every currency's yield curve
void Pricer::ApplyRateShift( double Shift )
{
    for ( Currency* c : _currency_set )
    {
        c->GetRate()->SetCurveShift( Shift );
    }
}

//! engines that can price one contract in isolation override this; the base
//! refuses (MCL prices the whole correlated book at once and never calls it).
void Pricer::PriceContract( Contract* Ctr )
{
    ERR( "book pricing '" + _name + "': engine does not support per-contract pricing for '" +
         Ctr->GetName() + "'" );
}

//! shared contract loop for the per-contract engines (PDE, ANA): one progress
//! bar over the contracts, each step pricing a contract and (when requested)
//! its bump-and-revalue Greeks. The book/pricer totals are accumulated as each
//! contract completes, so the bar reflects the real per-contract work.
void Pricer::PriceBookByContract( const string& Label )
{
    Pricer::InitPricing();

    const bool greeks = _request_delta || _request_gamma || _request_vega ||
                        _request_rho || _request_theta;

    //! pricer-level Greek totals are summed over contracts here (reset first)
    _delta = _gamma = _vega = _rho = _theta = 0;

    long done = 0;
    ProgressBar bar( Label, (long)_book->GetOptionList().size() );
    for ( Contract* c : _book->GetOptionList() )
    {
        cancellation::CancellationPoint(); //!< abort promptly if the client disconnected

        PriceContract( c ); //!< base price for this contract
        if ( greeks )
        {
            ComputeContractGreeks( c );
        }

        AggregateContract( c ); //!< premium (+ bump delta/gamma) -> book totals

        //! book-currency Greek totals (delta/gamma already folded into the book
        //! by AggregateContract; vega/rho/theta live at pricer level)
        const double fx = FxToBook( c );
        _delta += c->GetDelta() * fx;
        _gamma += c->GetGamma() * fx;
        _vega += c->GetVega() * fx;
        _rho += c->GetRho() * fx;
        _theta += c->GetTheta() * fx;

        bar.Update( ++done );
    }
    bar.Done();
}

//! per-contract bump-and-revalue Greeks: bump only this contract's market and
//! reprice just this contract. Mirrors the book-level ComputeGreeks bumps but
//! scoped to one contract, storing the results on the contract. Restores the
//! contract's base price on exit (so AggregateContract sees the unbumped premium).
void Pricer::ComputeContractGreeks( Contract* Ctr )
{
    const double p0 = Ctr->GetPremium();

    double delta = 0;
    double gamma = 0;

    //! for a multi-asset underlying on a grid engine (PDE), the basket "spot" is a
    //! fixed rebased 100 that a per-component bump can't move, so the bump would
    //! read zero — keep the grid's own dV/dS delta/gamma (set by PriceContract) instead.
    const bool grid_spot = GridSpotGreeks() && Ctr->GetSingleSet().size() > 1;

    //! delta / gamma : per-underlying relative spot bump, summed over the
    //! contract's underlyings (small bump for delta, wider one for gamma)
    if ( ( _request_delta || _request_gamma ) && !grid_spot )
    {
        for ( Single* s : Ctr->GetSingleSet() )
        {
            const double spot = s->GetSpot();

            if ( _request_delta ) //!< one-sided forward difference, reuses p0
            {
                const double h = GREEK_SPOT_BUMP * spot;
                s->SetSpot( spot + h );
                PriceContract( Ctr );
                const double pu = Ctr->GetPremium();
                s->SetSpot( spot ); //!< restore
                delta += ( pu - p0 ) / h;
            }

            if ( _request_gamma )
            {
                const double h = GREEK_GAMMA_BUMP * spot;
                s->SetSpot( spot + h );
                PriceContract( Ctr );
                const double pu = Ctr->GetPremium();
                s->SetSpot( spot - h );
                PriceContract( Ctr );
                const double pd = Ctr->GetPremium();
                s->SetSpot( spot ); //!< restore
                gamma += ( pu - 2 * p0 + pd ) / ( h * h );
            }
        }
    }

    //! vega : one-sided parallel vol bump on the contract's underlyings, per vol point
    double vega = 0;
    if ( _request_vega )
    {
        const SingleSet singles = Ctr->GetSingleSet();
        for ( Single* s : singles )
        {
            s->GetVolatility()->SetVolShift( GREEK_VOL_BUMP );
        }
        PriceContract( Ctr );
        const double pu = Ctr->GetPremium();
        for ( Single* s : singles )
        {
            s->GetVolatility()->SetVolShift( 0 ); //!< restore
        }
        vega = ( pu - p0 ) / GREEK_VOL_BUMP * 0.01;
    }

    //! rho : one-sided parallel rate bump on every currency the contract touches
    //! (premium currency + the underlyings' currencies, each shifted once), per 1%
    double rho = 0;
    if ( _request_rho )
    {
        CurrencySet ccys = Ctr->GetUnderlying()->GetCurrencySet();
        ccys.insert( Ctr->GetPremiumCurrency() );
        for ( Currency* c : ccys )
        {
            c->GetRate()->SetCurveShift( GREEK_RATE_BUMP );
        }
        PriceContract( Ctr );
        const double pu = Ctr->GetPremium();
        for ( Currency* c : ccys )
        {
            c->GetRate()->SetCurveShift( 0 ); //!< restore
        }
        rho = ( pu - p0 ) / GREEK_RATE_BUMP * 0.01;
    }

    //! theta : roll one calendar day forward (spot held), per-day decay. The PDE
    //! grid keys its time axis off the pricer _today, while ANA reads the
    //! contract's date, so roll both (and restore both).
    double theta = 0;
    if ( _request_theta )
    {
        const date base = _today;
        _today = base + days( 1 );
        Ctr->SetToday( _today );
        PriceContract( Ctr );
        const double p1 = Ctr->GetPremium();
        _today = base; //!< restore
        Ctr->SetToday( _today );
        theta = p1 - p0;
    }

    //! restore the contract to its base price BEFORE writing the Greeks: the
    //! restore reprice (PDE) sets delta/gamma from the grid, so set ours after —
    //! except in grid_spot mode, where we deliberately keep the grid's delta/gamma.
    PriceContract( Ctr );
    if ( !grid_spot )
    {
        Ctr->SetDelta( delta );
        Ctr->SetGamma( gamma );
    }
    Ctr->SetVega( vega );
    Ctr->SetRho( rho );
    Ctr->SetTheta( theta );
}

//! bump-and-revalue Greeks. Every scenario re-runs PriceBook with one market
//! input bumped; for Monte-Carlo the random sequence is reset each run so the
//! scenarios share common random numbers and the differences stay stable.
void Pricer::ComputeGreeks()
{
    //! every PriceBook below is an inner bump revaluation: silence the engine's
    //! per-sweep progress bar and status logs (restored on the way out). One
    //! "GRK" bar replaces them, advancing once per bump so the whole Greek job
    //! shows as a single line instead of N repeated re-price blocks.
    _quiet_pricing = true;

    const double p0 = _premium;

    //! count the bump scenarios up front to size the Greek progress bar:
    //! delta = 1 / underlying, gamma = 2 / underlying, vega/rho/theta = 1 each,
    //! plus one final restore re-price.
    const long ns = (long)_single_set.size();
    long greek_total = 1; //!< final restore
    if ( _request_delta )
        greek_total += ns;
    if ( _request_gamma )
        greek_total += 2 * ns;
    if ( _request_vega )
        greek_total += 1;
    if ( _request_rho )
        greek_total += 1;
    if ( _request_theta )
        greek_total += 1;
    ProgressBar greek_bar( "GRK", greek_total );
    long greek_done = 0;

    //! delta / gamma : per-underlying relative spot bump (summed over underlyings),
    //! with a small bump for delta and a wider one for gamma (see GREEK_*_BUMP).
    //! snapshot the singles first: PriceBook -> InitPricing rebuilds _single_set,
    //! which would invalidate an iterator held over it here.
    if ( _request_delta || _request_gamma )
    {
        const vector<Single*> singles( _single_set.begin(), _single_set.end() );
        double delta = 0;
        double gamma = 0;
        for ( Single* s : singles )
        {
            const double spot = s->GetSpot();

            if ( _request_delta ) //!< one-sided forward difference, reuses p0
            {
                const double h = GREEK_SPOT_BUMP * spot;
                s->SetSpot( spot + h );
                PriceBook();
                greek_bar.Update( ++greek_done );
                const double pu = _book->GetPremium();
                s->SetSpot( spot ); //!< restore
                delta += ( pu - p0 ) / h;
            }

            if ( _request_gamma )
            {
                const double h = GREEK_GAMMA_BUMP * spot;
                s->SetSpot( spot + h );
                PriceBook();
                greek_bar.Update( ++greek_done );
                const double pu = _book->GetPremium();
                s->SetSpot( spot - h );
                PriceBook();
                greek_bar.Update( ++greek_done );
                const double pd = _book->GetPremium();
                s->SetSpot( spot ); //!< restore
                gamma += ( pu - 2 * p0 + pd ) / ( h * h );
            }
        }
        _delta = delta;
        _gamma = gamma;
    }

    //! vega : one-sided parallel vol bump, reported per 1 vol point (0.01 of vol)
    if ( _request_vega )
    {
        ApplyVolShift( GREEK_VOL_BUMP );
        PriceBook();
        greek_bar.Update( ++greek_done );
        const double pu = _book->GetPremium();

        ApplyVolShift( 0 ); //!< restore
        _vega = ( pu - p0 ) / GREEK_VOL_BUMP * 0.01;
    }

    //! rho : one-sided parallel rate bump, reported per 1% (0.01) rate move
    if ( _request_rho )
    {
        ApplyRateShift( GREEK_RATE_BUMP );
        PriceBook();
        greek_bar.Update( ++greek_done );
        const double pu = _book->GetPremium();

        ApplyRateShift( 0 ); //!< restore
        _rho = ( pu - p0 ) / GREEK_RATE_BUMP * 0.01;
    }

    //! theta : roll today forward one calendar day (spot held), per-day decay
    if ( _request_theta )
    {
        const date base_today = _today;
        _today = base_today + days( 1 );
        PriceBook();
        greek_bar.Update( ++greek_done );
        const double p1 = _book->GetPremium();
        _today = base_today; //!< restore
        _theta = p1 - p0;
    }

    //! restore the book to the base scenario so the premium output is unbumped
    PriceBook();
    greek_bar.Update( ++greek_done );
    greek_bar.Done();
    _premium = _book->GetPremium();

    _quiet_pricing = false;
}

//!
void Pricer::WriteResults()
{

    //! display final price + trust (+ any requested Greeks)
    string greeks;
    if ( _request_delta )
    {
        greeks += ", delta = " + ToString( _delta );
    }
    if ( _request_gamma )
    {
        greeks += ", gamma = " + ToString( _gamma );
    }
    if ( _request_vega )
    {
        greeks += ", vega = " + ToString( _vega );
    }
    if ( _request_rho )
    {
        greeks += ", rho = " + ToString( _rho );
    }
    if ( _request_theta )
    {
        greeks += ", theta = " + ToString( _theta );
    }
    for ( const auto& [param, value] : _param_greeks )
    {
        greeks += ", vega_" + param + " = " + ToString( value );
    }
    LOG( "BPR",
         "book price = " + ToString( _premium ) + ", " +
             "book trust = " + ToString( _book->GetPremiumTrust() ) + greeks + ", " +
             "book time = " + ToString( _exec_time ) + "sec" );

    //! write cfg results
    Task::WriteResults();
    _cfg->SetDouble( _result + ".premium", _premium );
    _cfg->SetDouble( _result + ".premium_trust", _book->GetPremiumTrust() );

    //! requested Greeks (book level), computed by bump-and-revalue
    if ( _request_delta )
    {
        _cfg->SetDouble( _result + ".delta", _delta );
    }
    if ( _request_gamma )
    {
        _cfg->SetDouble( _result + ".gamma", _gamma );
    }
    if ( _request_vega )
    {
        _cfg->SetDouble( _result + ".vega", _vega );
    }
    if ( _request_rho )
    {
        _cfg->SetDouble( _result + ".rho", _rho );
    }
    if ( _request_theta )
    {
        _cfg->SetDouble( _result + ".theta", _theta );
    }
    //! model-parameter Greeks (book level), keyed vega_<param>
    for ( const auto& [param, value] : _param_greeks )
    {
        _cfg->SetDouble( _result + ".vega_" + param, value );
    }

    //! per-contract premium (and, for the per-contract engines, per-contract
    //! Greeks attributed to each option)
    const bool per_contract_greeks = GreeksPerContract();
    for ( Contract* c : _book->GetOptionList() )
    {
        const string p = _result + "." + c->GetName();
        _cfg->SetDouble( p + "_premium", c->GetPremium() );
        _cfg->SetDouble( p + "_premium_trust", c->GetPremiumTrust() );

        if ( per_contract_greeks )
        {
            if ( _request_delta )
            {
                _cfg->SetDouble( p + "_delta", c->GetDelta() );
            }
            if ( _request_gamma )
            {
                _cfg->SetDouble( p + "_gamma", c->GetGamma() );
            }
            if ( _request_vega )
            {
                _cfg->SetDouble( p + "_vega", c->GetVega() );
            }
            if ( _request_rho )
            {
                _cfg->SetDouble( p + "_rho", c->GetRho() );
            }
            if ( _request_theta )
            {
                _cfg->SetDouble( p + "_theta", c->GetTheta() );
            }
        }
    }
}

//! getting fixing dates
set<date> Pricer::GetFixingDates()
{
    set<date> s1 = _book->GetFixingDates(); //! fixing dates
    s1.insert( _today );
    return s1;
}

//! FX factor converting a contract premium into the book currency
double Pricer::FxToBook( Contract* Ctr )
{
    if ( _currency == Ctr->GetPremiumCurrency() )
    {
        return 1;
    }
    //! converting a premium across currencies needs the FX spot
    if ( !_correlation )
    {
        ERR( "book pricing '" + _name + "' has multi-currency premiums and requires a correlation matrix for FX conversion" );
    }
    return _correlation->GetFxSpot( _currency->GetName(),
                                    Ctr->GetPremiumCurrency()->GetName() );
}

//! add a priced contract's premium/delta/gamma to the book (with FX conversion)
void Pricer::AggregateContract( Contract* Ctr )
{
    double fx = FxToBook( Ctr );
    _book->SetPremium( _book->GetPremium() + Ctr->GetPremium() * fx );
    _book->SetDelta( _book->GetDelta() + Ctr->GetDelta() * fx );
    _book->SetGamma( _book->GetGamma() + Ctr->GetGamma() * fx );
}

//! verify every contract in the book supports the engine's pricing method
void Pricer::CheckAllowed( const std::function<bool( Contract* )>& HasSolution,
                           const string& MethodLabel )
{
    for ( Contract* c : _book->GetOptionList() )
    {
        if ( !HasSolution( c ) )
        {
            ERR( c->GetName() + " ( " + c->GetKind() + " ) can't be priced with the " +
                 MethodLabel + " method" );
        }
    }
}

//! init objects before pricing
void Pricer::InitPricing()
{

    //! set today
    _book->SetToday( _today );
    _currency->SetToday( _today );

    //! reset book accumulators so re-pricing does not double-count (the engines
    //! aggregate via SetPremium(GetPremium() + ...))
    _book->SetPremium( 0 );
    _book->SetPremiumTrust( 0 );
    _book->SetDelta( 0 );
    _book->SetGamma( 0 );

    //! set correlation to underlyings (and reset per-contract accumulators)
    vector<Contract*> option_list = _book->GetOptionList();
    vector<Contract*>::iterator c;
    for ( c = option_list.begin();
          c != option_list.end();
          c++ )
    {
        ( *c )->SetCorrelation( _correlation );
        ( *c )->GetUnderlying()->SetCorrelation( _correlation );
        ( *c )->SetPremium( 0 );
        ( *c )->SetPremiumTrust( 0 );
        ( *c )->SetDelta( 0 );
        ( *c )->SetGamma( 0 );
        ( *c )->SetVega( 0 );
        ( *c )->SetRho( 0 );
        ( *c )->SetTheta( 0 );
    }

    // underlying lists
    _single_set = _book->GetSingleSet();
    _currency_set = _book->GetCurrencySet();

    //! set underlyings today -> redundant
    SingleSet::iterator s;
    for ( s = _single_set.begin();
          s != _single_set.end();
          s++ )
    {
        ( *s )->SetToday( _today );

        //! Heston: the spot/variance correlation rho lives in the global
        //! correlation matrix (variance keyed "<underlying>_var"), not on the
        //! heston_volatility object. Resolve it onto the vol so every engine can
        //! read hv->GetRho() unchanged.
        if ( ( *s )->GetVolatility()->IsStochastic() )
        {
            HestonVolatility* hv = dynamic_cast<HestonVolatility*>( ( *s )->GetVolatility() );
            if ( hv )
            {
                if ( !_correlation )
                {
                    ERR( "book pricing '" + _name + "': Heston underlying '" + ( *s )->GetName() +
                         "' needs a correlation matrix for its spot/variance correlation" );
                }
                hv->SetRho( _correlation->GetValue( ( *s )->GetName(), ( *s )->GetName() + "_var" ) );
            }
        }
    }
}