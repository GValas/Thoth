#pragma once
#include "currency.hpp"
#include "single.hpp"
#include "volatility.hpp"

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
