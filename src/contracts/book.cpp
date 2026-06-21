#include "thoth.hpp"
#include "book.hpp"
#include "object_reader.hpp"
#include "single.hpp" //!< complete Single for the aggregated SingleSet

//! constructor (members are initialised in-class)
Book::Book( const string& ObjectName ) : Object( ObjectName, KIND_BOOK ) {}

//! destructor
Book::~Book() = default;

//! read the list of contracts (resolved as Contract references)
void Book::Configure( ObjectReader& reader )
{
    SetOptionList( reader.Ref<vector<Contract>>( "options" ) );
}

//! setter
void Book::SetOptionList( const vector<Contract*>& OptionList )
{
    _option_list = OptionList;
}

//! setter
void Book::SetPremium( double Premium )
{
    _premium = Premium;
}

//! setter
void Book::SetPremiumTrust( double PremiumTrust )
{
    _premium_trust = PremiumTrust;
}

//! setter
void Book::SetDelta( double Delta )
{
    _delta = Delta;
}

//! setter
void Book::SetGamma( double Gamma )
{
    _gamma = Gamma;
}

//! setter
void Book::SetToday( const date& Today )
{
    for ( Contract* c : _option_list )
    {
        c->SetToday( Today );
    }
}

//! getter (the stored list, by const reference: it is iterated in hot Greek loops)
const vector<Contract*>& Book::GetOptionList() const
{
    return _option_list;
}

//! getter
double Book::GetPremium() const
{
    return _premium;
}

//! getter
double Book::GetPremiumTrust() const
{
    return _premium_trust;
}

//! getter
double Book::GetDelta() const
{
    return _delta;
}

//! getter
double Book::GetGamma() const
{
    return _gamma;
}

//! getter
double Book::GetVegaBS() const
{
    return _vega_bs;
}

//! getter
double Book::GetVolgaBS() const
{
    return _volga_bs;
}

//! get fixing dates from each contract
set<date> Book::GetFixingDates()
{
    set<date> set_dates;
    for ( Contract* c : _option_list )
    {
        set<date> s = c->GetFixingDates();
        set_dates.insert( s.begin(), s.end() );
    }
    return set_dates;
}

//! return american exercise dates
set<date> Book::GetAmericanExerciseDates()
{
    set<date> set_dates;
    for ( Contract* c : _option_list )
    {
        set<date> s = c->GetAmericanExerciseDates();
        set_dates.insert( s.begin(), s.end() );
    }
    return set_dates;
}

//! list of single names
SingleSet Book::GetSingleSet() const
{
    SingleSet s;
    for ( Contract* c : _option_list )
    {
        SingleSet s_ = c->GetSingleSet();
        s.insert( s_.begin(), s_.end() );
    }
    return s;
}

//! list of currency names
CurrencySet Book::GetCurrencySet() const
{
    CurrencySet s;
    for ( Contract* c : _option_list )
    {
        CurrencySet s_ = c->GetUnderlying()->GetCurrencySet();
        s.insert( c->GetPremiumCurrency() );
        s.insert( s_.begin(), s_.end() );
    }
    return s;
}

MonteCarloNode* Book::GetNode( NodeCollector& NC )
{
    return NC.GetOrCreate<BookNode>(
        _name,
        [&]( BookNode* B )
        {
            for ( Contract* c : _option_list )
            {
                //! FX conversion to the book currency is applied at aggregation
                //! (AggregateContract, PDE/ANA); the MCL book sums premiums in the
                //! contract currency (BookNode defaults the FX factor to 1, so no
                //! per-contract node_name::FX node is created).
                B->PushContractNode( c->GetNode( NC ) );
            }
        } );
}