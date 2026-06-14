#pragma once
#include "underlying.hpp"

class Mono : public Underlying
{

  private:
    Single* _single = nullptr;

  public:
    //! fwd & vol
    double GetForward( const date& MaturityDate,
                       Currency* QuantoCurrency ) override;
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    // getter
    SingleSet GetSingleSet() override;
    CurrencySet GetCurrencySet() override;
    double GetSpot() override;

    //! setter
    void SetSingle( Single& Single );

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    MonteCarloNode* GetVolNode( NodeCollector& NC ) override;
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency,
                                   const string& BaseCurrency ) override;

    //! destructor, constructor
    Mono( const string& ObjectName,
          const string& ObjectKind );
    ~Mono() override;
};
