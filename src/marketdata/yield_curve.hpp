#pragma once
#include "curve.hpp"

class YieldCurve : public Curve
{

  public:
    //!
    double GetDiscountFactor( const date& MaturityDate );

    //! contructor, destructor
    YieldCurve( const string& ObjectName );
    ~YieldCurve() override;
};
