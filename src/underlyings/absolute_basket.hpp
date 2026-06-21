#pragma once
#include "basket.hpp"

//! A weighted-average basket sum_i w_i * S_i of its components, diffused as the
//! weighted sum of the per-component spot paths.
class AbsoluteBasket : public Basket
{

  private:
    //! attributes
    LaVector _weight_list;

  public:
    //! read own fields (component list + weights), then capture the rebasing
    //! reference spots at load
    void Configure( ObjectReader& reader ) override;

    //! setter
    void SetWeightList( la_vector* WeightList );

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

    //!
    AbsoluteBasket( const string& ObjectName );
    ~AbsoluteBasket() override;
};
