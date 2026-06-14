#include "thoth.hpp"
#include "continuous_dividends_curve.hpp"

ContinuousDividendsCurve::ContinuousDividendsCurve( const string& ObjectName ) : Curve( ObjectName, KIND_CONTINUOUS_DIVIDENDS_CURVE )
{
}

ContinuousDividendsCurve::~ContinuousDividendsCurve() = default;
