#pragma once
#include "curve.hpp"

//! A discount (zero-rate) curve: a Curve that also yields the discount factor
//! exp(-r * YearFraction(today, T)) to a maturity.
class YieldCurve : public Curve
{

  public:
    //!
    double GetDiscountFactor( const date& MaturityDate );

    //! contructor, destructor
    YieldCurve( const string& ObjectName );
    ~YieldCurve() override;
};
