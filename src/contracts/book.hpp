#pragma once
#include "contract.hpp"
#include "object.hpp"

//! A book: the list of contracts priced together, holding the aggregated premium
//! and Greeks (in the book currency) and exposing the union of fixing / exercise
//! dates and the single-name / currency sets the engines need.
class Book : public Object
{
    //
  private:
    //! attributes
    vector<Contract*> _option_list; //!< contracts priced together (non-owning)
    Currency* _currency = nullptr;  //!< book reporting currency

    //! premium: aggregated PV and its Monte-Carlo standard error ("trust")
    double _premium = 0;
    double _premium_trust = 0;

    //! greeks: aggregated value (delta / gamma; the rest are per-contract)
    double _delta = 0;
    double _gamma = 0;

    //
  public:
    //! read own field (the list of contracts priced together)
    void Configure( ObjectReader& reader ) override;

    //! setter — store the aggregated premium (PV)
    void SetPremium( double Premium );
    //! setter — store the premium's MC standard error
    void SetPremiumTrust( double PremiumTrust );
    //! setter — store the aggregated delta
    void SetDelta( double Delta );
    //! setter — store the aggregated gamma
    void SetGamma( double Gamma );
    //! cascade the valuation date to every contract in the book
    void SetToday( const date& Today ) override;

    //! getter
    const vector<Contract*>& GetOptionList() const;
    double GetPremium() const;
    double GetPremiumTrust() const;
    double GetDelta() const;
    double GetGamma() const;

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
