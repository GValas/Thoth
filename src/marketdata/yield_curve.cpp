#include "thoth.hpp"
#include "yield_curve.hpp"

YieldCurve::YieldCurve( const string& ObjectName ) : Curve( ObjectName, KIND_YIELD_CURVE )
{
}

YieldCurve::~YieldCurve() = default;

double YieldCurve::GetDiscountFactor( const date& MaturityDate )
{
    double r = GetCurveValue( MaturityDate );
    double dt = YearFraction( _today, MaturityDate );
    return exp( -r * dt );
}