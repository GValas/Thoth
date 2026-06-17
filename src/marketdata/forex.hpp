#pragma once
#include "currency.hpp"
#include "single.hpp"
#include "volatility.hpp"

//! An FX pair (underlying currency vs base currency): the spot rate and its (flat)
//! volatility, used for quanto / composite FX conversion in the node graph.
class Forex : public Single
{

    //
  private:
    //! attributes
    Currency* _underlying_currency;

  public:
    //! setter
    void SetUnderlyingCurrency( Currency& UnderlyingCurrency );
    void SetBaseCurrency( Currency& BaseCurrency );

    //! getter
    bool IsForex() const override { return true; }
    Currency* GetBaseCurrency();
    Currency* GetUnderlyingCurrency();
    double GetForward( const date& MaturityDate ) override;
    double GetConstantVol() const;
    double GetLocalVolatility( const double Strike,
                               const date& MaturityDate ) override;

    //! mcl node
    MonteCarloNode* GetDriftNode( NodeCollector& NC ) override;

    //! constructor
    Forex( const string& ObjectName );
    ~Forex() override;
};
