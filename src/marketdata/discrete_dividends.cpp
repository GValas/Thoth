#include "thoth.hpp"
#include "discrete_dividends.hpp"
#include "object_reader.hpp"

//! discrete_dividends.cpp — DiscreteDividends implementation (plain data accessors).
//! No discounting/PV logic lives here: that needs the equity's curve and is done in
//! Equity (DiscreteDividendsPv / FutureDividendPv / GetDividendNode).

//! constructor — tag with the discrete-dividends kind; the schedule is read in Configure
DiscreteDividends::DiscreteDividends( const string& ObjectName ) : MarketData( ObjectName, KIND_DISCRETE_DIVIDENDS )
{
}

DiscreteDividends::~DiscreteDividends() = default;

//! read own fields — the ex-dividend dates and their cash amounts (index-aligned)
void DiscreteDividends::Configure( ObjectReader& reader )
{
    SetDates( reader.Get<vector<date>>( "dates" ) );
    SetAmounts( reader.Get<vector<double>>( "amounts" ) );
}

//! setter — store the ex-dividend dates (assumed ascending, paired with amounts)
void DiscreteDividends::SetDates( const vector<date>& Dates )
{
    _dates = Dates;
}

//! setter — store the per-date cash amounts (in the equity's currency)
void DiscreteDividends::SetAmounts( const vector<double>& Amounts )
{
    _amounts = Amounts;
}

//! getter — ex-dividend dates
const vector<date>& DiscreteDividends::GetDates() const
{
    return _dates;
}

//! getter — cash amounts per ex-date
const vector<double>& DiscreteDividends::GetAmounts() const
{
    return _amounts;
}
