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

class Single;
class Currency;
class Forex;
class Underlying;

template <class T>
struct ByName
{
    bool operator()( const T* a, const T* b ) const { return a->GetName() < b->GetName(); }
};

using SingleSet = std::set<Single*, ByName<Single>>;
using CurrencySet = std::set<Currency*, ByName<Currency>>;
using ForexSet = std::set<Forex*, ByName<Forex>>;
using UnderlyingSet = std::set<Underlying*, ByName<Underlying>>;
