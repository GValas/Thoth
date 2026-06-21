#include "thoth.hpp"
#include "pricer.hpp"
#include "cancellation.hpp"
#include "market_data.hpp" //!< RISK_FACTOR_VOL / RISK_FACTOR_RATE
#include "object_reader.hpp"
#include "progress_bar.hpp"

//! constructor (members are initialised in-class)
Pricer::Pricer( const string& ObjectName,
                YamlConfig& YamlConfig ) : Task( ObjectName, YamlConfig, KIND_PRICER ) {}

//! destructor
Pricer::~Pricer() = default;

//! read the fields common to every pricer (the concrete class was already chosen
//! by the registry, which drives the type off the configuration's method)
void Pricer::Configure( ObjectReader& reader )
{
    SetCurrency( *reader.Ref<Currency>( "currency" ) );
    SetBook( *reader.Ref<Book>( "book" ) );
    SetToday( reader.Get<date>( "today" ) );
    SetConfiguration( *reader.Ref<PricerConfiguration>( "configuration" ) );
    SetIndicatorRequestList( reader.Get<vector<string>>( "indicators" ) );
    SetResult( reader.Get<string>( "result" ) );
    if ( reader.Has<string>( "correlation" ) )
    {
        SetCorrelation( reader.Ref<Correlation>( "correlation" ) );
    }
    if ( reader.Has<string>( "debug_configuration" ) )
    {
        SetDebugConfiguration( reader.Ref<DebugConfiguration>( "debug_configuration" ) );
    }
}

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
            if ( v->HasFactor( param ) )
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
            v->ApplyShift( param, GREEK_PARAM_BUMP );
        }
        PriceBook();
        const double pu = _book->GetPremium();
        for ( Volatility* v : vols )
        {
            v->ApplyShift( param, 0 ); //!< restore
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
        s->GetVolatility()->ApplyShift( RISK_FACTOR_VOL, Shift );
    }
}

//! parallel shift applied to every currency's yield curve
void Pricer::ApplyRateShift( double Shift )
{
    for ( Currency* c : _currency_set )
    {
        c->GetRate()->ApplyShift( RISK_FACTOR_RATE, Shift );
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
    //! disable the bar during an inner bump-revalue (ComputeParamGreeks reprices the
    //! whole book per parameter): otherwise each reprice redraws a full "<engine> 100%"
    //! line. The base price (Execute) runs with _quiet_pricing = false, so it shows.
    ProgressBar bar( Label, (long)_book->GetOptionList().size(), !_quiet_pricing );
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
        _delta += c->Result().delta * fx;
        _gamma += c->Result().gamma * fx;
        _vega += c->Result().vega * fx;
        _rho += c->Result().rho * fx;
        _theta += c->Result().theta * fx;

        bar.Update( ++done );
    }
    bar.Done();
}

//! shared finite-difference bump-and-revalue engine (see the header for the
//! contract). Inputs are restored as each Greek finishes; the premium is left at
//! the last (theta) bump, so the caller does the final restore reprice itself.
Pricer::BumpGreeks Pricer::BumpAndRevalueGreeks( double P0,
                                                 const vector<Single*>& Singles,
                                                 const CurrencySet& Currencies,
                                                 bool DoDelta,
                                                 bool DoGamma,
                                                 bool DoVega,
                                                 bool DoRho,
                                                 bool DoTheta,
                                                 const std::function<double()>& Reprice,
                                                 const std::function<void( const date& )>& RollToday,
                                                 const std::function<void()>& Tick )
{
    BumpGreeks g;

    //! delta / gamma : per-underlying relative spot bump, summed over the
    //! underlyings (small bump for delta, wider one for gamma)
    if ( DoDelta || DoGamma )
    {
        for ( Single* s : Singles )
        {
            const double spot = s->GetSpot();

            if ( DoDelta ) //!< one-sided forward difference, reuses P0
            {
                const double h = GREEK_SPOT_BUMP * spot;
                s->SetSpot( spot + h );
                _graph_tree_tag = "delta"; //!< capture this reprice's node graph
                const double pu = Reprice();
                _graph_tree_tag.clear();
                Tick();
                s->SetSpot( spot ); //!< restore
                g.delta += ( pu - P0 ) / h;
            }

            if ( DoGamma )
            {
                const double h = GREEK_GAMMA_BUMP * spot;
                s->SetSpot( spot + h );
                _graph_tree_tag = "gamma"; //!< one graph for gamma (the up bump)
                const double pu = Reprice();
                _graph_tree_tag.clear();
                Tick();
                s->SetSpot( spot - h );
                const double pd = Reprice();
                Tick();
                s->SetSpot( spot ); //!< restore
                g.gamma += ( pu - 2 * P0 + pd ) / ( h * h );
            }
        }
    }

    //! vega : one-sided parallel vol bump, per 1 vol point (0.01 of vol)
    if ( DoVega )
    {
        for ( Single* s : Singles )
        {
            s->GetVolatility()->ApplyShift( RISK_FACTOR_VOL, GREEK_VOL_BUMP );
        }
        _graph_tree_tag = "vega";
        const double pu = Reprice();
        _graph_tree_tag.clear();
        Tick();
        for ( Single* s : Singles )
        {
            s->GetVolatility()->ApplyShift( RISK_FACTOR_VOL, 0 ); //!< restore
        }
        g.vega = ( pu - P0 ) / GREEK_VOL_BUMP * 0.01;
    }

    //! rho : one-sided parallel rate bump on every currency, per 1% (0.01)
    if ( DoRho )
    {
        for ( Currency* c : Currencies )
        {
            c->GetRate()->ApplyShift( RISK_FACTOR_RATE, GREEK_RATE_BUMP );
        }
        _graph_tree_tag = "rho";
        const double pu = Reprice();
        _graph_tree_tag.clear();
        Tick();
        for ( Currency* c : Currencies )
        {
            c->GetRate()->ApplyShift( RISK_FACTOR_RATE, 0 ); //!< restore
        }
        g.rho = ( pu - P0 ) / GREEK_RATE_BUMP * 0.01;
    }

    //! theta : roll one calendar day forward (spot held), per-day decay
    if ( DoTheta )
    {
        const date base = _today;
        RollToday( base + days( 1 ) );
        _graph_tree_tag = "theta";
        const double p1 = Reprice();
        _graph_tree_tag.clear();
        Tick();
        RollToday( base ); //!< restore the valuation date
        g.theta = p1 - P0;
    }

    return g;
}

//! per-contract bump-and-revalue Greeks: bump only this contract's market and
//! reprice just this contract (via the shared engine), storing the results on the
//! contract. Restores the contract's base price on exit (so AggregateContract
//! sees the unbumped premium).
void Pricer::ComputeContractGreeks( Contract* Ctr )
{
    const double p0 = Ctr->Result().premium;

    //! for a multi-asset underlying on a grid engine (PDE), the basket "spot" is a
    //! fixed rebased 100 that a per-component bump can't move, so the bump would
    //! read zero — keep the grid's own dV/dS delta/gamma (set by PriceContract) instead.
    const bool grid_spot = GridSpotGreeks() && Ctr->GetSingleSet().size() > 1;

    //! the contract's own underlyings and the currencies it touches (premium
    //! currency + the underlyings' currencies)
    const SingleSet ss = Ctr->GetSingleSet();
    const vector<Single*> singles( ss.begin(), ss.end() );
    CurrencySet ccys = Ctr->GetUnderlying()->GetCurrencySet();
    ccys.insert( Ctr->GetPremiumCurrency() );

    //! the PDE grid keys its time axis off the pricer _today while ANA reads the
    //! contract's date, so theta rolls both.
    const BumpGreeks g = BumpAndRevalueGreeks(
        p0, singles, ccys,
        _request_delta && !grid_spot, _request_gamma && !grid_spot,
        _request_vega, _request_rho, _request_theta,
        [this, Ctr]
        { PriceContract( Ctr ); return Ctr->Result().premium; },
        [this, Ctr]( const date& d )
        { _today = d; Ctr->SetToday( d ); },
        [] {} );

    //! restore the contract to its base price BEFORE writing the Greeks: the
    //! restore reprice (PDE) sets delta/gamma from the grid, so set ours after —
    //! except in grid_spot mode, where we deliberately keep the grid's delta/gamma.
    PriceContract( Ctr );
    if ( !grid_spot )
    {
        Ctr->Result().delta = g.delta;
        Ctr->Result().gamma = g.gamma;
    }
    Ctr->Result().vega = g.vega;
    Ctr->Result().rho = g.rho;
    Ctr->Result().theta = g.theta;
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

    //! snapshot the singles first: PriceBook -> InitPricing rebuilds _single_set,
    //! which would invalidate an iterator held over it through the bumps.
    const vector<Single*> singles( _single_set.begin(), _single_set.end() );

    //! whole-book bump-and-revalue: reprice the book, bump every underlying /
    //! currency, roll only the pricer _today for theta.
    const BumpGreeks g = BumpAndRevalueGreeks(
        p0, singles, _currency_set,
        _request_delta, _request_gamma, _request_vega, _request_rho, _request_theta,
        [this]
        { PriceBook(); return _book->GetPremium(); },
        [this]( const date& d )
        { _today = d; },
        [&]
        { greek_bar.Update( ++greek_done ); } );

    _delta = g.delta;
    _gamma = g.gamma;
    _vega = g.vega;
    _rho = g.rho;
    _theta = g.theta;

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

    //! debug node graph : one Graphviz .dot per MC tree (base + Greek scenarios),
    //! emitted as <tree>_mcl_graph (e.g. premium_mcl_graph, delta_mcl_graph). The
    //! Dump() emitter renders multi-line scalars as literal blocks, so the .dot text
    //! stays readable (no \n / \" escapes).
    for ( const auto& [tree, dot] : _tree_graphs )
    {
        _cfg->SetString( _result + "." + tree + "_mcl_graph", dot );
    }

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
        _cfg->SetDouble( p + "_premium", c->Result().premium );
        _cfg->SetDouble( p + "_premium_trust", c->Result().premium_trust );

        if ( per_contract_greeks )
        {
            if ( _request_delta )
            {
                _cfg->SetDouble( p + "_delta", c->Result().delta );
            }
            if ( _request_gamma )
            {
                _cfg->SetDouble( p + "_gamma", c->Result().gamma );
            }
            if ( _request_vega )
            {
                _cfg->SetDouble( p + "_vega", c->Result().vega );
            }
            if ( _request_rho )
            {
                _cfg->SetDouble( p + "_rho", c->Result().rho );
            }
            if ( _request_theta )
            {
                _cfg->SetDouble( p + "_theta", c->Result().theta );
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
    _book->SetPremium( _book->GetPremium() + Ctr->Result().premium * fx );
    _book->SetDelta( _book->GetDelta() + Ctr->Result().delta * fx );
    _book->SetGamma( _book->GetGamma() + Ctr->Result().gamma * fx );
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
        ( *c )->Result().premium = 0;
        ( *c )->Result().premium_trust = 0;
        ( *c )->Result().delta = 0;
        ( *c )->Result().gamma = 0;
        ( *c )->Result().vega = 0;
        ( *c )->Result().rho = 0;
        ( *c )->Result().theta = 0;
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
        //! stochastic-vol object. Resolve it onto the vol (SetStochasticRho) so
        //! every engine reads it back via StochasticParams() unchanged.
        if ( ( *s )->GetVolatility()->IsStochastic() )
        {
            if ( !_correlation )
            {
                ERR( "book pricing '" + _name + "': stochastic-vol underlying '" + ( *s )->GetName() +
                     "' needs a correlation matrix for its spot/variance correlation" );
            }
            ( *s )->GetVolatility()->SetStochasticRho(
                _correlation->GetValue( ( *s )->GetName(), ( *s )->GetName() + "_var" ) );
        }
    }
}