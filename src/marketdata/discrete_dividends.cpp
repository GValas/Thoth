#include "thoth.hpp"
#include "discrete_dividends.hpp"
#include "object_reader.hpp"

DiscreteDividends::DiscreteDividends( const string& ObjectName ) : MarketData( ObjectName, KIND_DISCRETE_DIVIDENDS )
{
}

DiscreteDividends::~DiscreteDividends() = default;

//! read own fields (ex-dates + cash amounts)
void DiscreteDividends::Configure( ObjectReader& reader )
{
    SetDates( reader.Get<vector<date>>( "dates" ) );
    SetAmounts( reader.Get<vector<double>>( "amounts" ) );
}

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
