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

//! read the list of contracts (resolved as Contract references) into the name-ordered
//! set (membership is the same as the YAML list; only the iteration order is by name)
void Book::Configure( ObjectReader& reader )
{
    vector<Contract*> contracts = reader.Ref<vector<Contract>>( "contracts" );
    _contract_set.insert( contracts.begin(), contracts.end() );
}

//! cascade the valuation date: the book has no state of its own to roll, so it
//! just re-anchors each contract (which in turn rolls its currency and underlying)
void Book::SetToday( const date& Today )
{
    for ( Contract* c : _contract_set )
    {
        c->SetToday( Today );
    }
}

//! re-anchor the book before a (re-)price: cascade the valuation date and the
//! correlation to every contract (and its underlying). The book holds no priced
//! state of its own — the engine (Pricer) owns and clears the results.
void Book::Reset( const date& Today, Correlation* Correl )
{
    for ( Contract* c : _contract_set )
    {
        c->SetToday( Today );
        c->SetCorrelation( Correl );
        c->GetUnderlying()->SetCorrelation( Correl );
    }
}

//! getter (the stored set, by const reference: it is iterated in hot Greek loops)
const ContractSet& Book::GetContractSet() const
{
    return _contract_set;
}

//! get fixing dates from each contract
set<date> Book::GetFixingDates()
{
    set<date> set_dates;
    for ( Contract* c : _contract_set )
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
    for ( Contract* c : _contract_set )
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
    for ( Contract* c : _contract_set )
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
            for ( Contract* c : _contract_set )
            {
                //! FX conversion to the book currency is applied at aggregation
                //! (AggregateContract, PDE/ANA); the MCL book sums premiums in the
                //! contract currency (BookNode defaults the FX factor to 1, so no
                //! per-contract node_name::FX node is created).
                B->PushContractNode( c->GetNode( NC ) );
            }
        } );
}