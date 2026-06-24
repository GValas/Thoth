#pragma once
#include "basket.hpp"

//! rainbow.hpp — best-of / worst-of (rainbow) basket.
//!
//! Unlike AbsoluteBasket (a *sum* of components, which moment-matches to one
//! lognormal), a rainbow's payoff is the order statistic max/min of the rebased
//! component performances. That depends on the components' full joint law, not a
//! single marginal, so no equivalent forward/vol exists and the analytic/PDE
//! engines (which assume one forward + one vol) cannot handle it — only the
//! Monte-Carlo engine can, via a RainbowNode that picks max/min per path.

//! best-of / worst-of (rainbow) basket: its spot is the max (best-of) or min
//! (worst-of) of the member underlyings' rebased performances (S_i / S_i0),
//! scaled to 100. Because the payoff is on a max/min — not a single lognormal —
//! it is priced by the Monte-Carlo engine only (a RainbowNode); ANA/PDE, which
//! assume one forward + vol, reject it.
class Rainbow : public Basket
{

  private:
    //! best-of (max) vs worst-of (min) selection; defaults to best-of until Configure.
    RainbowType _type = RainbowType::BestOf;

  public:
    //! read own fields (component list + best/worst-of type), then capture the
    //! rebasing reference spots at load
    void Configure( ObjectReader& reader ) override;

    //! setter — select best-of / worst-of.
    void SetType( RainbowType Type );

    //! a max/min payoff is not single-lognormal: no 1-D grid / closed form
    //! (overrides Basket, which is griddable). MCL-only.
    bool IsGriddable() const override { return false; }

    //! mcl node
    //! the rainbow spot node: a RainbowNode taking max/min over rebased member spots.
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    //! not implemented — a rainbow has no single composite vol node (throws).
    MonteCarloNode* GetVolNode( NodeCollector& NC ) override;
    //! not implemented — no quanto correlation node for a rainbow (throws).
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency,
                                   const string& BaseCurrency ) override;

    //! fwd & vol (MCL-only : ANA/PDE not supported for a max/min payoff)
    //! t0 spot = 100 (every rebased performance is 1 at inception).
    double GetSpot() const override;
    //! throws: no closed-form forward for a max/min payoff (MCL-only).
    double GetForward( const date& MaturityDate,
                       Currency* QuantoCurrency ) override;
    //! throws: no closed-form vol for a max/min payoff (MCL-only).
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //! ctor — KIND_RAINBOW; dtor.
    Rainbow( const string& ObjectName );
    ~Rainbow() override;
};
