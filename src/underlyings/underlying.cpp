#include "thoth.hpp"
#include "underlying.hpp"

//! underlying.cpp — base-class plumbing for Underlying. Only the non-pure members
//! live here: construction, the correlation setter/getter. All pricing behaviour is
//! pure-virtual and supplied by the concrete shapes (Single/Composite/Basket/...).

//! base ctor: forward name + kind to Asset and null the not-yet-injected handles.
//! _correlation is set later via SetCorrelation; _currency is set by the concrete
//! shape (e.g. Composite::Configure) so the base leaves it null.
Underlying::Underlying( const string& ObjectName,
                        const string& ObjectKind ) : Asset( ObjectName, ObjectKind )
{
    _correlation = nullptr;
    _currency = nullptr;
}

//! nothing owned here (correlation/currency are non-owning references), so default.
Underlying::~Underlying() = default;
////
//////! setter
////void Underlying::SetCurrency( Currency * Currency )
////{
////    _currency = Currency;
////}
////

//! setter — store the shared correlation matrix. Base implementation only records
//! the handle; multi-asset / composite shapes override to first propagate it to
//! their sub-underlyings, then call this base to record their own copy.
void Underlying::SetCorrelation( Correlation* Correlation )
{
    _correlation = Correlation;
}

////! getter
// Currency * Underlying::GetCurrency()
//{
//     return _currency;
// }

//! getter — the injected correlation matrix (nullptr until SetCorrelation runs).
Correlation* Underlying::GetCorrelation() const
{
    return _correlation;
}
