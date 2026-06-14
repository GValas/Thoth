#include "thoth.hpp"
#include "underlying.hpp"

//!
Underlying::Underlying( const string& ObjectName,
                        const string& ObjectKind ) : Asset( ObjectName, ObjectKind )
{
    _correlation = nullptr;
    _currency = nullptr;
}

//!
Underlying::~Underlying() = default;
////
//////! setter
////void Underlying::SetCurrency( Currency * Currency )
////{
////    _currency = Currency;
////}
////

//! setter
void Underlying::SetCorrelation( Correlation* Correlation )
{
    _correlation = Correlation;
}

////! getter
// Currency * Underlying::GetCurrency()
//{
//     return _currency;
// }

//! getter
Correlation* Underlying::GetCorrelation()
{
    return _correlation;
}
