#include "thoth.hpp"
#include "repo_curve.hpp"

//! repo_curve.cpp — trivial Curve specialisation (kind tag only).

//! constructor — forwards the name and stamps the repo-curve kind so the curve is
//! read as a repo/borrow rate; all data/interpolation is inherited from Curve
RepoCurve::RepoCurve( const string& ObjectName ) : Curve( ObjectName, KIND_REPO_CURVE )
{
}

//! destructor
RepoCurve::~RepoCurve() = default;
