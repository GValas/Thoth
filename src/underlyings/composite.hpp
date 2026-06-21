#pragma once
#include "underlying.hpp"

//! A composite (quanto) underlying: an asset quoted in one currency but settled in
//! another at the prevailing FX (S*FX). Combines the underlying with the FX leg and
//! a quanto drift correction in the node graph.
class Composite : public Underlying
{

  private:
    //! attributes
    Underlying* _underlying;

  public:
    //! read own fields (wrapped underlying + the composite/settlement currency)
    void Configure( ObjectReader& reader ) override;

    //! setter
    void SetCompoCurrency( Currency& CompositeCurrency );
    void SetUnderlying( Underlying& Underlying );
    void SetCorrelation( Correlation* Correlation ) override;

    //! getter
    Underlying* GetUnderlying() const;
    Currency* GetCompositeCurrency() const;
    SingleSet GetSingleSet() const override;
    CurrencySet GetCurrencySet() const override;

    //! a single (quanto'd) asset: collapses to one spatial dimension
    bool IsGriddable() const override { return true; }

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    MonteCarloNode* GetVolNode( NodeCollector& NC ) override;
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency,
                                   const string& BaseCurrency ) override;

    //! fwd & vol
    double GetSpot() const override;
    double GetForward( const date& MaturityDate,
                       Currency* QuantoCurrency ) override;
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //! constructor, destructor
    Composite( const string& ObjectName );
    ~Composite() override;
};
