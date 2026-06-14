#pragma once
#include "yield_curve.hpp"

class Currency : public Object
{
  private:
    //! attribute
    YieldCurve* _rate;

  public:
    //! setter
    void SetRate( YieldCurve& Rate );
    void SetToday( const date& Today ) override;

    //! getter
    YieldCurve* GetRate();

    //! mcl node
    MonteCarloNode* GetDiscFactorNode( NodeCollector& NC );
    MonteCarloNode* GetRateNode( NodeCollector& NC );

    //! constructor, destructor
    Currency( const string& ObjectName );
    ~Currency() override;
};
