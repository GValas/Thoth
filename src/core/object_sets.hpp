#pragma once

//! ----------------------------------------------------------------------
//! Deterministic, pointer-independent sets of domain objects.
//!
//! Pricing iterates sets of singles / currencies / exchange-rates /
//! underlyings to build the Monte Carlo node graph and assign Sobol
//! dimensions and noise draws.  Ordering those sets by raw pointer makes the
//! result depend on heap addresses; ordering by object name instead makes
//! pricing reproducible across builds and platforms.  Object names are unique
//! per object, so the set contents are unchanged — only the iteration order.
//! ----------------------------------------------------------------------

#include <set>

//! forward declarations only — the sets store pointers, so the full types are not
//! needed here (and GetName is resolved at the point of use, not in this header).
class Single;
class Currency;
class Forex;
class Underlying;

//! strict-weak ordering of object pointers by their (unique) name. Using the name
//! rather than the pointer value is what makes the set order deterministic: heap
//! addresses vary run-to-run, names do not. Names are unique, so this is a total
//! order with no collisions — set membership is identical to a pointer-keyed set,
//! only the iteration order differs.
template <class T>
struct ByName
{
    bool operator()( const T* a, const T* b ) const { return a->GetName() < b->GetName(); }
};

//! the concrete name-ordered sets iterated during MC graph construction / Sobol
//! dimension and noise assignment (see the file header for why ordering matters).
using SingleSet = std::set<Single*, ByName<Single>>;
using CurrencySet = std::set<Currency*, ByName<Currency>>;
using ForexSet = std::set<Forex*, ByName<Forex>>;
using UnderlyingSet = std::set<Underlying*, ByName<Underlying>>;
