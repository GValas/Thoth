#pragma once
#include "yield_curve.hpp"

//! A currency and its discount (yield) curve: provides discount factors / rates and
//! the MCL rate / discount-factor nodes.
class Currency : public Object
{
  private:
    //! attribute
    YieldCurve* _rate;

  public:
    //! read own field (the discount/yield curve)
    void Configure( ObjectReader& reader ) override;

    //! setter
    void SetRate( YieldCurve& Rate );
    void SetToday( const date& Today ) override;

    //! getter
    YieldCurve* GetRate() const;

    //! mcl node
    MonteCarloNode* GetDiscFactorNode( NodeCollector& NC );
    MonteCarloNode* GetRateNode( NodeCollector& NC );

    //! constructor, destructor
    Currency( const string& ObjectName );
    ~Currency() override;
};
