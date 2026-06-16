#pragma once
#include "pricer.hpp"

//! Book pricing via closed-form (analytic) formulas.
//! Each contract is priced through its own ANA_EvalPrice() implementation
//! (e.g. Black-Scholes for european vanillas); no PDE grid is solved.

class PricerANA : public Pricer
{

  protected:
    void PreCheck_() override; //!< require a closed-form solution for every contract
    void PriceBook_() override;
    void PriceContract_( Contract* Ctr ) override; //!< single-contract closed-form price
    bool GreeksPerContract_() const override { return true; }

  public:
    //! constructor, destructor
    PricerANA( const string& ObjectName,
               YamlConfig& YamlConfig );
    ~PricerANA() override;
};
