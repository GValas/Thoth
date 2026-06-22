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
//! The one factory, shared by every kind. Build a bare T, register it in the collector
//! FIRST (Add returns a stable pointer, so a reference that resolves back to this same
//! object while it configures itself finds it), then let it read its own fields. A task
//! that needs the YamlConfig at construction (to write its result block back) takes
//! T(name, cfg); every other object takes T(name) — detected at compile time, so this
//! single function covers all of them. All field knowledge lives on the class.
template <class T>
Object* Create( ObjectManager& m, const string& name )
{
    std::unique_ptr<T> object;
    if constexpr ( std::is_constructible_v<T, const string&, YamlConfig&> )
        object = std::make_unique<T>( name, m.yml() );
    else
        object = std::make_unique<T>( name );

    T* registered = m.collector().Add( std::move( object ) );
    ObjectReader reader( m, registered->GetName() );
    registered->Configure( reader );
    return registered;
}
} // namespace

//! Dispatch a YAML !kind tag to the concrete type that handles it. This is the only
//! translation unit that knows every concrete class, so adding a type is one
//! { tag, &Create<T> } line below plus the class itself — ObjectManager stays
//! type-agnostic and calls Build on a cache miss. The table is a function-local static
//! (built once on first use, no global init-order issue).
Object* ObjectManager::Build( const string& ObjectName )
{
    using Factory = Object* ( * )( ObjectManager&, const string& ); //!< plain function pointer
    static const map<string, Factory> registry = {
        // ---- tasks (engine picked by the tag: !mcl_pricer / !pde_pricer / !ana_pricer) ----
        { KIND_MCL_PRICER, &Create<PricerMCL> },
        { KIND_PDE_PRICER, &Create<PricerPDE> },
        { KIND_ANA_PRICER, &Create<PricerANA> },
        { KIND_SEQUENCE, &Create<Sequence> },
        // ---- instruments ----
        { KIND_VANILLA, &Create<Vanilla> },
        { KIND_BARRIER, &Create<Barrier> },
        { KIND_VARIANCE_SWAP, &Create<VarianceSwap> },
        // ---- underlyings (an equity / forex is itself a single-asset underlying) ----
        { KIND_EQUITY, &Create<Equity> },
        { KIND_BASKET, &Create<AbsoluteBasket> },
        { KIND_RAINBOW, &Create<Rainbow> },
        { KIND_COMPOSITE, &Create<Composite> },
        { KIND_FOREX, &Create<Forex> },
        // ---- market data (the three curve kinds share Curve::Configure) ----
        { KIND_CURRENCY, &Create<Currency> },
        { KIND_YIELD_CURVE, &Create<YieldCurve> },
        { KIND_REPO_CURVE, &Create<RepoCurve> },
        { KIND_CONTINUOUS_DIVIDENDS_CURVE, &Create<ContinuousDividendsCurve> },
        { KIND_DISCRETE_DIVIDENDS, &Create<DiscreteDividends> },
        { KIND_BS_VOLATILITY, &Create<BsVolatility> },
        { KIND_HESTON_VOLATILITY, &Create<HestonVolatility> },
        { KIND_SABR_VOLATILITY, &Create<SabrVolatility> },
        { KIND_CORRELATION_MATRIX, &Create<Correlation> },
        // ---- configurations (engine-parameter objects, referenced by the pricers) ----
        { KIND_DEBUG_CONFIGURATION, &Create<DebugConfiguration> },
        { KIND_PDE_CONFIGURATION, &Create<PdeConfiguration> },
        { KIND_MCL_CONFIGURATION, &Create<MclConfiguration> },
        // ---- book & fixings ----
        { KIND_BOOK, &Create<Book> },
        { KIND_SIMPLE_FIXING_DATA, &Create<SimpleFixingData> },
    };

    const string kind = _yml.GetTag( ObjectName ); //!< the !kind tag on the YAML node
    auto entry = registry.find( kind );
    if ( entry == registry.end() )
    {
        ERR( "unknown kind : " + kind ); //!< no factory for this tag -> configuration error
    }
    return entry->second( *this, ObjectName ); //!< create + register + configure
}
