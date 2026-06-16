#pragma once
#include "curve.hpp"

//! A repo-rate curve (a borrow cost subtracted from the equity drift); a Curve read
//! as a continuously-compounded rate.
class RepoCurve : public Curve
{

  public:
    RepoCurve( const string& ObjectName );
    ~RepoCurve() override;
};
