#include "thoth.hpp"
#include "book.hpp"
#include "object_reader.hpp"
#include "single.hpp" //!< complete Single for the aggregated SingleSet

//! Book implementation: configuration, premium/Greek accessors, the date and name
//! unions the engines need, and the Monte-Carlo BookNode that sums the contract
//! sub-trees. The aggregated premium/Greeks are written by the pricing engines via
//! the setters and read back by the output reader.

//! constructor (members are initialised in-class)
Book::Book( const string& ObjectName ) : Object( ObjectName, KIND_BOOK ) {}

//! destructor
Book::~Book() = default;

//! read the list of contracts (resolved as Contract references)
void Book::Configure( ObjectReader& reader )
{
    _option_list = reader.Ref<vector<Contract>>( "options" );
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

//! cascade the valuation date: the book has no state of its own to roll, so it
//! just re-anchors each contract (which in turn rolls its currency and underlying)
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

//! list of currency names: per contract, its premium (settlement) currency plus
//! every currency its underlying's legs are quoted in (so the engine builds all the
//! needed rate / FX curves)
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

//! Build (or fetch) the BookNode: the Monte-Carlo aggregate of all contract nodes.
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