#include "thoth.hpp"
#include "yield_curve.hpp"

//! yield_curve.cpp — YieldCurve implementation: discount factor from the zero rate.

//! constructor — tag the Curve with the yield-curve kind; pillars come from Curve
YieldCurve::YieldCurve( const string& ObjectName ) : Curve( ObjectName, KIND_YIELD_CURVE )
{
}

YieldCurve::~YieldCurve() = default;

//! discount factor DF(T) = exp(-r(T) * tau): r is the interpolated continuously-
//! compounded zero rate at MaturityDate (rho shift already folded in by GetCurveValue),
//! and tau is the ACT-based year fraction from the valuation date _today to MaturityDate
double YieldCurve::GetDiscountFactor( const date& MaturityDate ) const
{
    double r = GetCurveValue( MaturityDate ); //!< zero rate (with any rho bump applied)
    double dt = YearFraction( _today, MaturityDate );
    return exp( -r * dt );
}