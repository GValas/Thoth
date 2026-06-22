#include "thoth.hpp"
#include "continuous_dividends_curve.hpp"

//! continuous_dividends_curve.cpp — trivial Curve specialisation (kind tag only).

//! constructor — forwards the name and stamps the continuous-dividend kind so the
//! curve is read as a dividend yield q(T); all data/interp comes from Curve
ContinuousDividendsCurve::ContinuousDividendsCurve( const string& ObjectName ) : Curve( ObjectName, KIND_CONTINUOUS_DIVIDENDS_CURVE )
{
}

ContinuousDividendsCurve::~ContinuousDividendsCurve() = default;
