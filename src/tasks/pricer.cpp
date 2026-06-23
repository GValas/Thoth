#include "thoth.hpp"
#include "pricer.hpp"
#include "cancellation.hpp"
#include "market_data.hpp" //!< RISK_FACTOR_VOL / RISK_FACTOR_RATE
#include "object_reader.hpp"
#include "progress_bar.hpp"

//! constructor (members are initialised in-class). The concrete kind is supplied by
//! the subclass (KIND_MCL_PRICER / KIND_PDE_PRICER / KIND_ANA_PRICER).
Pricer::Pricer( const string& ObjectName,
                YamlConfig& YamlConfig,
                const string& ObjectKind ) : Task( ObjectName, YamlConfig, ObjectKind ) {}

//! destructor
Pricer::~Pricer() = default;

//! read the fields common to every pricer (the concrete class was already chosen
//! by the registry straight off the YAML tag: !mcl_pricer / !pde_pricer / !ana_pricer)
void Pricer::Configure( ObjectReader& reader )
{
    SetCurrency( reader.Ref<Currency>( "currency" ) );                     //!< reporting currency
    SetBook( reader.Ref<Book>( "book" ) );                                 //!< the contracts to price
    SetToday( reader.Get<date>( "today" ) );                               //!< valuation date
    SetIndicatorRequestList( reader.Get<vector<string>>( "indicators" ) ); //!< premium / Greeks to compute
    SetResult( reader.Get<string>( "result" ) );                           //!< where to write the output block
    //! correlation is optional (single-asset single-ccy books need none); resolved
    //! by reference when present
    if ( reader.Has<string>( "correlation" ) )
    {
        SetCorrelation( reader.Ref<Correlation>( "correlation" ) );
    }
    //! debug switches are optional (default: all off)
    if ( reader.Has<string>( "debug_configuration" ) )
    {
        SetDebugConfiguration( reader.Ref<DebugConfiguration>( "debug_configuration" ) );
    }
}

// setter : the book of contracts this pricer values (borrowed, owned by the manager)
void Pricer::SetBook( Book* Book )
{
    _book = Book;
}

// setter : optional correlation matrix (needed for multi-asset diffusion, quanto and
// FX premium conversion; may stay null for a single-currency single-asset book)
void Pricer::SetCorrelation( Correlation* Correlation )
{
    _correlation = Correlation;
}

// setter : record the requested indicators and derive the per-greek flags from them
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

// setter : the book (reporting) currency every premium / Greek is converted into
void Pricer::SetCurrency( Currency* Currency )
{
    _currency = Currency;
}

//! getter : the valuation date (the as-of date the whole book is priced at)
date Pricer::GetToday() const
{
    return _today;
}

//! top-level orchestration shared by every engine: validate once, price the base
//! scenario, then layer the requested Greeks on top. The concrete engine supplies
//! PriceBook / ComputeGreeks / GreeksPerContract.
void Pricer::Execute()
{
    //! validate the book / configuration once for the chosen engine
    PreCheck();

    double t0 = WallClockSeconds();

    //! base scenario : price the whole book at the quoted market. For the
    //! per-contract engines (PDE, ANA) PriceBook already folds in each
    //! contract's Greeks inside its contract loop, so ComputeGreeks is skipped.
    PriceBook();

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

    _task_time = TaskTime( t0 );
}

//! model-parameter Greeks: for each requested vega_<param>, bump that parameter on
//! every underlying whose vol surface exposes it, reprice the whole book, and take
//! the one-sided finite difference (per unit of the parameter). Skips a parameter
//! no surface in the book has (e.g. vega_alpha on a Heston book). Book-level, so it
//! works the same for MCL (re-diffuses with common random numbers) and ANA / PDE.
void Pricer::ComputeParamGreeks()
{
    _quiet_pricing = true;
    const double p0 = _book_result.premium;

    //! preserve the base book result (premium + already-computed Greeks): every
    //! PriceBook below runs InitPricing, which zeroes the book aggregate, so it is
    //! restored from this snapshot once the param-Greek sweep is done.
    const Valuation base_result = _book_result;

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
        const double pu = _book_result.premium;
        for ( Volatility* v : vols )
        {
            v->ApplyShift( param, 0 ); //!< restore
        }
        _param_greeks[param] = ( pu - p0 ) / GREEK_PARAM_BUMP;
    }

    //! restore the book to the base scenario so the per-contract premiums are
    //! unbumped, then restore the book aggregate (premium + Greeks) from the snapshot
    PriceBook();
    _book_result = base_result;
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

    //! the book aggregate (premium + every Greek) is zeroed by InitPricing above;
    //! AggregateContract folds in premium/delta/gamma, vega/rho/theta are summed below.

    long done = 0;
    //! disable the bar during an inner bump-revalue (ComputeParamGreeks reprices the
    //! whole book per parameter): otherwise each reprice redraws a full "<engine> 100%"
    //! line. The base price (Execute) runs with _quiet_pricing = false, so it shows.
    ProgressBar bar( Label, (long)_book->GetContractSet().size(), !_quiet_pricing );
    for ( Contract* c : _book->GetContractSet() )
    {
        cancellation::CancellationPoint(); //!< abort promptly if the client disconnected

        PriceContract( c ); //!< base price for this contract
        if ( greeks )
        {
            ComputeContractGreeks( c );
        }

        AggregateContract( c ); //!< premium (+ bump delta/gamma) -> book aggregate

        //! book-currency Greek totals (delta/gamma already folded into the book
        //! aggregate by AggregateContract; vega/rho/theta summed here)
        const double fx = FxToBook( c );
        _book_result.vega += Result( c ).vega * fx;
        _book_result.rho += Result( c ).rho * fx;
        _book_result.theta += Result( c ).theta * fx;

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
    const double p0 = Result( Ctr ).premium;

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
        { PriceContract( Ctr ); return Result( Ctr ).premium; },
        [this, Ctr]( const date& d )
        { _today = d; Ctr->SetToday( d ); },
        [] {} );

    //! restore the contract to its base price BEFORE writing the Greeks: the
    //! restore reprice (PDE) sets delta/gamma from the grid, so set ours after —
    //! except in grid_spot mode, where we deliberately keep the grid's delta/gamma.
    PriceContract( Ctr );
    if ( !grid_spot )
    {
        Result( Ctr ).delta = g.delta;
        Result( Ctr ).gamma = g.gamma;
    }
    Result( Ctr ).vega = g.vega;
    Result( Ctr ).rho = g.rho;
    Result( Ctr ).theta = g.theta;
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

    const double p0 = _book_result.premium;

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
        { PriceBook(); return _book_result.premium; },
        [this]( const date& d )
        { _today = d; },
        [&]
        { greek_bar.Update( ++greek_done ); } );

    //! restore the book to the base scenario so the premium output is unbumped.
    //! Do this BEFORE storing the Greeks: PriceBook -> InitPricing zeroes the book
    //! aggregate, so Greeks written earlier would be wiped.
    PriceBook();
    greek_bar.Update( ++greek_done );
    greek_bar.Done();

    _book_result.delta = g.delta;
    _book_result.gamma = g.gamma;
    _book_result.vega = g.vega;
    _book_result.rho = g.rho;
    _book_result.theta = g.theta;

    _quiet_pricing = false;
}

//! log the book-level result line and serialise everything into the YAML result
//! block: premium + trust, optional node graphs, book-level Greeks, model-parameter
//! Greeks, and the per-contract premiums (plus per-contract Greeks for the
//! per-contract engines).
void Pricer::WriteResults()
{

    //! display final price + trust (+ any requested Greeks)
    string greeks;
    if ( _request_delta )
    {
        greeks += ", delta = " + ToString( _book_result.delta );
    }
    if ( _request_gamma )
    {
        greeks += ", gamma = " + ToString( _book_result.gamma );
    }
    if ( _request_vega )
    {
        greeks += ", vega = " + ToString( _book_result.vega );
    }
    if ( _request_rho )
    {
        greeks += ", rho = " + ToString( _book_result.rho );
    }
    if ( _request_theta )
    {
        greeks += ", theta = " + ToString( _book_result.theta );
    }
    for ( const auto& [param, value] : _param_greeks )
    {
        greeks += ", vega_" + param + " = " + ToString( value );
    }
    LOG( "BPR",
         "book price = " + ToString( _book_result.premium ) + ", " +
             "book trust = " + ToString( _book_result.premium_trust ) + greeks + ", " +
             "book time = " + ToString( _task_time ) + "sec" );

    //! write cfg results
    Task::WriteResults();
    WriteResult( "premium", _book_result.premium );
    WriteResult( "premium_trust", _book_result.premium_trust );

    //! debug node graph : one Graphviz .dot per MC tree (base + Greek scenarios),
    //! emitted as <tree>_mcl_graph (e.g. premium_mcl_graph, delta_mcl_graph). The
    //! Dump() emitter renders multi-line scalars as literal blocks, so the .dot text
    //! stays readable (no \n / \" escapes).
    for ( const auto& [tree, dot] : _tree_graphs )
    {
        WriteResult( tree + "_mcl_graph", dot );
    }

    //! requested Greeks (book level), computed by bump-and-revalue
    if ( _request_delta )
    {
        WriteResult( "delta", _book_result.delta );
    }
    if ( _request_gamma )
    {
        WriteResult( "gamma", _book_result.gamma );
    }
    if ( _request_vega )
    {
        WriteResult( "vega", _book_result.vega );
    }
    if ( _request_rho )
    {
        WriteResult( "rho", _book_result.rho );
    }
    if ( _request_theta )
    {
        WriteResult( "theta", _book_result.theta );
    }
    //! model-parameter Greeks (book level), keyed vega_<param>
    for ( const auto& [param, value] : _param_greeks )
    {
        WriteResult( "vega_" + param, value );
    }

    //! per-contract premium (and, for the per-contract engines, per-contract
    //! Greeks attributed to each option), keyed <contract>_<metric>
    const bool per_contract_greeks = GreeksPerContract();
    for ( Contract* c : _book->GetContractSet() )
    {
        const string n = c->GetName();
        WriteResult( n + "_premium", Result( c ).premium );
        WriteResult( n + "_premium_trust", Result( c ).premium_trust );

        if ( per_contract_greeks )
        {
            if ( _request_delta )
            {
                WriteResult( n + "_delta", Result( c ).delta );
            }
            if ( _request_gamma )
            {
                WriteResult( n + "_gamma", Result( c ).gamma );
            }
            if ( _request_vega )
            {
                WriteResult( n + "_vega", Result( c ).vega );
            }
            if ( _request_rho )
            {
                WriteResult( n + "_rho", Result( c ).rho );
            }
            if ( _request_theta )
            {
                WriteResult( n + "_theta", Result( c ).theta );
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

//! add a priced contract's premium/delta/gamma to the book aggregate (FX-converted)
void Pricer::AggregateContract( Contract* Ctr )
{
    double fx = FxToBook( Ctr );
    _book_result.premium += Result( Ctr ).premium * fx;
    _book_result.delta += Result( Ctr ).delta * fx;
    _book_result.gamma += Result( Ctr ).gamma * fx;
}

//! verify every contract in the book supports the engine's pricing method
void Pricer::CheckAllowed( const std::function<bool( Contract* )>& HasSolution,
                           const string& MethodLabel )
{
    for ( Contract* c : _book->GetContractSet() )
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

    //! re-anchor the book (cascade today + correlation to every contract)
    _book->Reset( _today, _correlation );
    _currency->SetToday( _today );

    // underlying lists
    _single_set = _book->GetSingleSet();
    _currency_set = _book->GetCurrencySet();
    _contract_set = _book->GetContractSet();

    //! clear all priced state (the financial objects hold none — it lives here):
    //! a fresh book aggregate and a zeroed Valuation per contract, so re-pricing
    //! does not double-count (the engines aggregate additively) and Result().at()
    //! always resolves.
    _book_result = Valuation{};
    _contract_results.clear();
    for ( Contract* c : _contract_set )
    {
        _contract_results[c->GetName()] = Valuation{};
    }

    //! set underlyings today -> redundant
    for ( auto& s : _single_set )
    {
        s->SetToday( _today );

        //! Heston: the spot/variance correlation rho lives in the global
        //! correlation matrix (variance keyed "<underlying>_var"), not on the
        //! stochastic-vol object. Resolve it onto the vol (SetStochasticRho) so
        //! every engine reads it back via StochasticParams() unchanged.
        if ( s->GetVolatility()->IsStochastic() )
        {
            if ( !_correlation )
            {
                ERR( "book pricing '" + _name + "': stochastic-vol underlying '" + s->GetName() +
                     "' needs a correlation matrix for its spot/variance correlation" );
            }
            auto rho = _correlation->GetValue( s->GetName(), s->GetName() + "_var" );
            s->GetVolatility()->SetStochasticRho( rho );
        }
    }
}