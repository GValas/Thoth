#pragma once
#include "basket.hpp"

//! absolute_basket.hpp — the weighted-sum (vanilla) basket.
//!
//! Models a basket whose value is the weighted average of its components' rebased
//! performances, scaled to a 100-base index: B = 100 * sum_i w_i * S_i / S_i0.
//! Because that sum of (approximately) lognormals is itself unimodal, it is priced
//! either by Monte-Carlo (an AbsoluteBasketNode summing the per-component spot
//! nodes) or — analytically/PDE — by moment-matching the sum onto a single shifted
//! lognormal and reading off a BS-equivalent vol (see GetImplicitVol).

//! A weighted-average basket sum_i w_i * S_i of its components, diffused as the
//! weighted sum of the per-component spot paths.
class AbsoluteBasket : public Basket
{

  private:
    //! attributes
    //! per-component weights w_i (indexed parallel to Basket::_underlying_list).
    LaVector _weight_list;

  public:
    //! read own fields (component list + weights), then capture the rebasing
    //! reference spots at load
    void Configure( ObjectReader& reader ) override;

    //! setter — install the weight vector (must align with the component list order).
    void SetWeightList( la_vector* WeightList );

    //! mcl node
    //! the basket spot node: an AbsoluteBasketNode summing 100*w_i/S_i0 * (member node).
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    //! not implemented — a relative basket has no single composite vol node (throws).
    MonteCarloNode* GetVolNode( NodeCollector& NC ) override;
    //! not implemented — quanto correlation node for a relative basket (throws).
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency,
                                   const string& BaseCurrency ) override;

    //! fwd & vol
    //! the 100-base index spot at t0: 100 * sum_i w_i (each rebased perf is 1 at t0).
    double GetSpot() const override;
    //! basket forward = 100 * sum_i w_i * F_i/S_i0 (forwards in QuantoCurrency).
    double GetForward( const date& MaturityDate,
                       Currency* QuantoCurrency ) override;
    //! BS-equivalent basket vol via 4-moment matching onto a shifted lognormal.
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //! ctor — KIND_BASKET; dtor.
    AbsoluteBasket( const string& ObjectName );
    ~AbsoluteBasket() override;
};
