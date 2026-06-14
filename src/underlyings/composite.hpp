#pragma once
#include "underlying.hpp"

class Composite : public Underlying
{

  private:
    //! attributes
    Underlying* _underlying;

  public:
    //! setter
    void SetCompoCurrency( Currency& CompositeCurrency );
    void SetUnderlying( Underlying& Underlying );
    void SetCorrelation( Correlation* Correlation ) override;

    //! getter
    Underlying* GetUnderlying();
    Currency* GetCompositeCurrency();
    SingleSet GetSingleSet() override;
    CurrencySet GetCurrencySet() override;

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    MonteCarloNode* GetVolNode( NodeCollector& NC ) override;
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency,
                                   const string& BaseCurrency ) override;

    //! fwd & vol
    double GetSpot() override;
    double GetForward( const date& MaturityDate,
                       Currency* QuantoCurrency ) override;
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //! constructor, destructor
    Composite( const string& ObjectName );
    ~Composite() override;
};
