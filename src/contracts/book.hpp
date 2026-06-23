#pragma once
#include "contract.hpp"
#include "object.hpp"

//! A book: the set of contracts priced together. A pure description — it holds no
//! priced result (the engine/Pricer owns premium + Greeks); it exposes the union of
//! fixing / exercise dates and the single-name / currency sets the engines need.
class Book : public Object
{
    //
  private:
    //! attributes
    ContractSet _contract_set;     //!< contracts priced together (non-owning)
    Currency* _currency = nullptr; //!< book reporting currency

    //
  public:
    //! read own field (the list of contracts priced together)
    void Configure( ObjectReader& reader ) override;

    //! cascade the valuation date to every contract in the book
    void SetToday( const date& Today ) override;
    //! re-anchor before a (re-)price: cascade today + correlation to every contract
    //! (the book holds no result state — the Pricer owns and clears the results).
    void Reset( const date& Today, Correlation* Correl );

    //! getter
    const ContractSet& GetContractSet() const;

    //! mcl — the BookNode summing the contract nodes (in their own currencies)
    MonteCarloNode* GetNode( NodeCollector& NC );

    //! access data — unions over the contracts
    set<date> GetFixingDates();

    //! the union of single names referenced by the book's contracts
    SingleSet GetSingleSet() const;
    //! the set of currencies (premium + underlying legs) used across the book
    CurrencySet GetCurrencySet() const;

    // ! destructeur, constructeur
    Book( const string& ObjectName );
    ~Book() override;
};
