#include "thoth.hpp"
#include "discrete_dividends.hpp"

DiscreteDividends::DiscreteDividends( const string& ObjectName ) : MarketData( ObjectName, KIND_DISCRETE_DIVIDENDS )
{
}

DiscreteDividends::~DiscreteDividends() = default;

void DiscreteDividends::SetDates( const vector<date>& Dates )
{
    _dates = Dates;
}

void DiscreteDividends::SetAmounts( const vector<double>& Amounts )
{
    _amounts = Amounts;
}

const vector<date>& DiscreteDividends::GetDates() const
{
    return _dates;
}

const vector<double>& DiscreteDividends::GetAmounts() const
{
    return _amounts;
}
