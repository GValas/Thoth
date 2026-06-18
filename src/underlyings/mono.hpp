#pragma once
#include "underlying.hpp"

//! A single-asset underlying wrapping one Single (equity): delegates the forward,
//! vol and node graph to it.
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
    SingleSet GetSingleSet() const override;
    CurrencySet GetCurrencySet() const override;
    double GetSpot() const override;
    double GetDiffusionSpot( const date& LastDate ) const override; //!< escrowed spot of the wrapped single

    //! a single-asset underlying: griddable and the canonical mono shape
    bool IsGriddable() const override { return true; }
    bool IsMono() const override { return true; }

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
