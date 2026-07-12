#pragma once
#include "thoth.hpp"

//! object.hpp — the common base of every configurable domain object.
//!
//! Every pricer, instrument, underlying, market-data and configuration entity in
//! the engine derives from Object. It carries the three universal attributes
//! (name, kind tag, valuation date) and the self-deserialisation hook
//! Configure(), so the registry can treat all types uniformly (create the bare
//! object by kind tag, then let it read its own fields/references).

class ObjectReader; //!< field reader passed to Configure (see object_reader.hpp)

//! Kind tags: the string each object carries in YAML (`name: !equity { ... }`)
//! and the key the registry (object_registry.cpp) dispatches on to pick a
//! factory. One constant per concrete type keeps the spelling in a single place,
//! shared between the YAML tag, the registry table and any code that tests kind.
inline constexpr char KIND_BARRIER[] = "barrier";
inline constexpr char KIND_VARIANCE_SWAP[] = "variance_swap";
inline constexpr char KIND_BOOK[] = "book";
//! the three pricer engines are each their own kind: the YAML tag (!mcl_pricer /
//! !pde_pricer / !ana_pricer) picks the concrete engine directly, the same way the
//! volatility / curve families do. There is no generic "pricer" kind.
inline constexpr char KIND_MCL_PRICER[] = "mcl_pricer"; //!< monte-carlo (longstaff-schwartz)
inline constexpr char KIND_PDE_PRICER[] = "pde_pricer"; //!< finite-difference PDE grid
inline constexpr char KIND_ANA_PRICER[] = "ana_pricer"; //!< closed-form (analytic) formulas
inline constexpr char KIND_BS_VOLATILITY[] = "bs_volatility";
inline constexpr char KIND_CONTINUOUS_DIVIDENDS_CURVE[] = "continuous_dividends_curve";
inline constexpr char KIND_DISCRETE_DIVIDENDS[] = "discrete_dividends";
inline constexpr char KIND_CORRELATION_MATRIX[] = "correlation_matrix";
inline constexpr char KIND_DEBUG_CONFIGURATION[] = "debug_configuration";
inline constexpr char KIND_CURRENCY[] = "currency";
inline constexpr char KIND_EQUITY[] = "equity";
inline constexpr char KIND_COMPOSITE[] = "composite";
inline constexpr char KIND_BASKET[] = "basket";
inline constexpr char KIND_RAINBOW[] = "rainbow";
inline constexpr char KIND_FOREX[] = "forex";
inline constexpr char KIND_HESTON_VOLATILITY[] = "heston_volatility";
inline constexpr char KIND_HULL_WHITE[] = "hull_white";
inline constexpr char KIND_LSV_VOLATILITY[] = "lsv_volatility";
inline constexpr char KIND_MCL_CONFIGURATION[] = "mcl_configuration";
inline constexpr char KIND_SABR_VOLATILITY[] = "sabr_volatility";
inline constexpr char KIND_PDE_CONFIGURATION[] = "pde_configuration";
inline constexpr char KIND_REPO_CURVE[] = "repo_curve";
inline constexpr char KIND_SEQUENCE[] = "sequence";
inline constexpr char KIND_SIMPLE_FIXING_DATA[] = "simple_fixing_data";
inline constexpr char KIND_AUTOCALLABLE[] = "autocallable";
inline constexpr char KIND_VANILLA[] = "vanilla";
inline constexpr char KIND_YIELD_CURVE[] = "yield_curve";

//! Base class of every domain object in the engine.
//!
//! Responsibilities: hold the identity (name + kind tag) and the valuation date,
//! and expose the polymorphic Configure() hook used by the registry to populate a
//! freshly created object from its YAML node. It is intentionally minimal — all
//! type-specific state and behaviour live in the derived classes.
//!
//! Invariant: a name is globally unique (the collector keys objects by name), and
//! the kind tag matches the registry entry that built the object.
class Object
{

  protected:
    //! attributes
    string _name; //!< globally unique identifier; also the YAML path prefix
    string _kind; //!< kind tag, one of the KIND_* constants above
    date _today;  //!< valuation ("as-of") date, propagated by the collector

  public:
    //! getter
    const string& GetName() const; //!< the unique name
    const string& GetKind() const; //!< the kind tag (a KIND_* value)

    //! setter — propagate the valuation date. virtual so a type with date-derived
    //! cached state can recompute it; the base just stores _today.
    virtual void SetToday( const date& Today );

    //! read this object's own fields / references from the configuration. The
    //! registry creates the bare object and calls this; concrete types override
    //! it (symmetrically to GetFlowNode, they own their own deserialisation).
    //! Default no-op so a not-yet-migrated type can keep a custom registry factory.
    virtual void Configure( ObjectReader& /*reader*/ ) {}

    //! constructor, destructor
    //! ObjectName / ObjectKind seed the immutable identity; _today is set later
    //! (after construction) via SetToday. virtual destructor so the collector can
    //! own derived objects through unique_ptr<Object>.
    Object( const string& ObjectName,
            const string& ObjectKind );
    virtual ~Object();
};
