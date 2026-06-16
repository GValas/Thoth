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
    vector<Contract*> _option_list;
    Currency* _currency = nullptr;

    //! premium
    double _premium = 0;
    double _premium_trust = 0;

    //! greeks
    double _delta = 0;
    double _delta_trust = 0;
    double _gamma = 0;
    double _gamma_trust = 0;
    double _vega_bs = 0;
    double _vega_trust = 0;
    double _volga_bs = 0;
    double _volga_trust = 0;

    //
  public:
    //! setter
    void SetOptionList( const vector<Contract*>& OptionList );
    void SetPremium( double Premium );
    void SetPremiumTrust( double PremiumTrust );
    void SetDelta( double Delta );
    void SetGamma( double Gamma );
    void SetToday( const date& Today ) override;

    //! getter
    vector<Contract*> GetOptionList() const;
    double GetPremium() const;
    double GetPremiumTrust() const;
    double GetDelta() const;
    double GetGamma() const;
    double GetVegaBS() const;
    double GetVolgaBS() const;

    //! mcl
    MonteCarloNode* GetNode( NodeCollector& NC );

    //! access data
    set<date> GetFixingDates();
    set<date> GetAmericanExerciseDates(); //! for american exercise

    SingleSet GetSingleSet();
    CurrencySet GetCurrencySet();

    // ! destructeur, constructeur
    Book( const string& ObjectName );
    ~Book() override;
};
