#include "thoth.hpp"
#include "object_manager.hpp"
#include "object_reader.hpp"

//! The single translation unit aware of every concrete object type. The kind
//! tag -> factory table below is the one place to touch when adding a type:
//! one entry here plus the new class, the ObjectManager stays type-agnostic.

#include "enums.hpp"

//! tasks
#include "pricer.hpp"
#include "pricer_ana.hpp"
#include "pricer_mcl.hpp"
#include "pricer_pde.hpp"
#include "pricer_configuration.hpp"
#include "debug_configuration.hpp"
#include "sequence.hpp"

//! instruments
#include "book.hpp"
#include "vanilla.hpp"
#include "barrier.hpp"
#include "variance_swap.hpp"

//! underlyings
#include "equity.hpp"
#include "absolute_basket.hpp"
#include "rainbow.hpp"
#include "composite.hpp"
#include "forex.hpp"

//! market data
#include "currency.hpp"
#include "yield_curve.hpp"
#include "repo_curve.hpp"
#include "continuous_dividends_curve.hpp"
#include "discrete_dividends.hpp"
#include "bs_volatility.hpp"
#include "sabr_volatility.hpp"
#include "heston_volatility.hpp"
#include "correlation.hpp"
#include "simple_fixing_data.hpp"

namespace
{
//! bind a field reader to an already-built object and let it read itself, returning
//! the object so a factory can keep wrapping / owning it. This is the whole of the
//! "object configures itself" step; the few factories that still need a custom
//! bootstrap (Equity's Mono wrap, the Pricer type selection, the cfg-owning tasks)
//! reuse it after their bespoke construction.
template <class T>
T* ConfigureObject( ObjectManager& m, T* o )
{
    ObjectReader reader( m, o->GetName() );
    o->Configure( reader );
    return o;
}

//! generic factory for a "configurable" object: create the bare object, register it
//! (so references can resolve back to it during configuration), then configure it.
//! The registry entry for such a type is reduced to a single index line; all field
//! knowledge lives on the class. A task that needs the YamlConfig at construction
//! (to write its result block back) is built with T(name, cfg) — detected here, so
//! both shapes share this one factory instead of a separate task variant.
template <class T>
ObjectManager::Factory MakeConfigurable()
{
    return []( ObjectManager& m, const string& n ) -> Object*
    {
        std::unique_ptr<T> o;
        if constexpr ( std::is_constructible_v<T, const string&, YamlConfig&> )
            o = std::make_unique<T>( n, m.cfg() );
        else
            o = std::make_unique<T>( n );
        return ConfigureObject( m, m.collector().Add( std::move( o ) ) );
    };
}

//! the configuration-selected pricer factory: the one bootstrap the registry must
//! keep, since a not-yet-created object cannot choose its own concrete class. Once
//! the right PricerXXX is built, the common fields are read by Pricer::Configure
//! like every other migrated object.
Object* BuildPricer( ObjectManager& m, const string& n )
{
    PricerConfiguration* PC = m.Get<PricerConfiguration>( m.cfg().GetString( n + ".configuration" ) );
    std::unique_ptr<Pricer> p;
    if ( PC->_method == PRICING_METHOD_MCL )
    {
        p = std::make_unique<PricerMCL>( n, m.cfg() );
    }
    else if ( PC->_method == PRICING_METHOD_MCL_GPU )
    {
        //! legacy alias: GPU is now an MCL capability gated by the mcl_configuration's
        //! allow_gpu flag. "mcl_gpu" forces that flag on, so old books keep
        //! accelerating; prefer `method: mcl` + `allow_gpu`.
        if ( PC->_mcl )
        {
            PC->_mcl->_allow_gpu = true;
        }
        LOG( "INI", "method 'mcl_gpu' is deprecated: use 'mcl' with 'allow_gpu: true' "
                    "in the mcl_configuration (treating it as that now)" );
        p = std::make_unique<PricerMCL>( n, m.cfg() );
    }
    else if ( PC->_method == PRICING_METHOD_PDE )
    {
        p = std::make_unique<PricerPDE>( n, m.cfg() );
    }
    else if ( PC->_method == PRICING_METHOD_ANA )
    {
        p = std::make_unique<PricerANA>( n, m.cfg() );
    }
    else
    {
        ERR( "pricing configuration '" + PC->GetName() + "' has unknown method '" +
             PC->_method + "' (expected '" + PRICING_METHOD_PDE + "', '" +
             PRICING_METHOD_MCL + "', '" + PRICING_METHOD_MCL_GPU + "' or '" +
             PRICING_METHOD_ANA + "')" );
    }
    return ConfigureObject( m, m.collector().Add( std::move( p ) ) );
}

//! build the kind -> factory table (one entry per object type). Every "self-
//! configuring" type is a single { kind, MakeConfigurable<T> } row — the class owns
//! all its field knowledge (including the cfg-owning tasks, which MakeConfigurable
//! detects). Only the pricer needs a hand-written factory (BuildPricer), to pick its
//! concrete engine type from the configuration before configuring it.
map<string, ObjectManager::Factory> MakeRegistry()
{
    return {
        // ---- tasks ----
        { KIND_PRICER, BuildPricer },
        { KIND_SEQUENCE, MakeConfigurable<Sequence>() },
        // ---- instruments ----
        { KIND_VANILLA, MakeConfigurable<Vanilla>() },
        { KIND_BARRIER, MakeConfigurable<Barrier>() },
        { KIND_VARIANCE_SWAP, MakeConfigurable<VarianceSwap>() },
        // ---- underlyings (an equity / forex is itself a single-asset underlying) ----
        { KIND_EQUITY, MakeConfigurable<Equity>() },
        { KIND_BASKET, MakeConfigurable<AbsoluteBasket>() },
        { KIND_RAINBOW, MakeConfigurable<Rainbow>() },
        { KIND_COMPOSITE, MakeConfigurable<Composite>() },
        { KIND_FOREX, MakeConfigurable<Forex>() },
        // ---- market data (the three curve kinds share Curve::Configure) ----
        { KIND_CURRENCY, MakeConfigurable<Currency>() },
        { KIND_YIELD_CURVE, MakeConfigurable<YieldCurve>() },
        { KIND_REPO_CURVE, MakeConfigurable<RepoCurve>() },
        { KIND_CONTINUOUS_DIVIDENDS_CURVE, MakeConfigurable<ContinuousDividendsCurve>() },
        { KIND_DISCRETE_DIVIDENDS, MakeConfigurable<DiscreteDividends>() },
        { KIND_BS_VOLATILITY, MakeConfigurable<BsVolatility>() },
        { KIND_HESTON_VOLATILITY, MakeConfigurable<HestonVolatility>() },
        { KIND_SABR_VOLATILITY, MakeConfigurable<SabrVolatility>() },
        { KIND_CORRELATION_MATRIX, MakeConfigurable<Correlation>() },
        // ---- configurations ----
        { KIND_PRICER_CONFIGURATION, MakeConfigurable<PricerConfiguration>() },
        { KIND_DEBUG_CONFIGURATION, MakeConfigurable<DebugConfiguration>() },
        { KIND_PDE_CONFIGURATION, MakeConfigurable<PdeConfiguration>() },
        { KIND_MCL_CONFIGURATION, MakeConfigurable<MclConfiguration>() },
        // ---- book & fixings ----
        { KIND_BOOK, MakeConfigurable<Book>() },
        { KIND_SIMPLE_FIXING_DATA, MakeConfigurable<SimpleFixingData>() },
    };
}
} // namespace

//! dispatch a name's kind tag to its factory (single registry, built once)
Object* ObjectManager::Build( const string& ObjectName )
{
    static const map<string, Factory> registry = MakeRegistry();

    const string kind = _yml.GetTag( ObjectName );
    auto entry = registry.find( kind );
    if ( entry == registry.end() )
    {
        ERR( "unknown kind : " + kind );
    }
    return entry->second( *this, ObjectName );
}
