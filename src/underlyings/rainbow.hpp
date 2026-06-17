#pragma once
#include "basket.hpp"

//! best-of / worst-of (rainbow) basket: its spot is the max (best-of) or min
//! (worst-of) of the member underlyings' rebased performances (S_i / S_i0),
//! scaled to 100. Because the payoff is on a max/min — not a single lognormal —
//! it is priced by the Monte-Carlo engine only (a RainbowNode); ANA/PDE, which
//! assume one forward + vol, reject it.
class Rainbow : public Basket
{

  private:
    RainbowType _type = RainbowType::BestOf;

  public:
    //! setter
    void SetType( RainbowType Type );

    //! a max/min payoff is not single-lognormal: no 1-D grid / closed form
    //! (overrides Basket, which is griddable). MCL-only.
    bool IsGriddable() const override { return false; }

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    MonteCarloNode* GetVolNode( NodeCollector& NC ) override;
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency,
                                   const string& BaseCurrency ) override;

    //! fwd & vol (MCL-only : ANA/PDE not supported for a max/min payoff)
    double GetSpot() override;
    double GetForward( const date& MaturityDate,
                       Currency* QuantoCurrency ) override;
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //!
    Rainbow( const string& ObjectName );
    ~Rainbow() override;
};
