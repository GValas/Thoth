#pragma once
#include "curve.hpp"

//! yield_curve.hpp — the discount (zero-rate) curve.
//!
//! A discount (zero-rate) curve: a Curve that also yields the discount factor
//! exp(-r * YearFraction(today, T)) to a maturity. It is the curve a Currency wraps;
//! the rho bump (inherited from Curve) shifts every zero rate, so a bumped discount
//! factor falls straight out of the same formula.
class YieldCurve : public Curve
{

  public:
    //! discount factor to MaturityDate: DF = exp(-r * tau) where r is the interpolated
    //! continuously-compounded zero rate and tau = YearFraction(today, MaturityDate)
    [[nodiscard]] double GetDiscountFactor( const date& MaturityDate ) const;

    //! contructor, destructor
    YieldCurve( const string& ObjectName );
    ~YieldCurve() override;
};
