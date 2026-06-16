#pragma once
#include "curve.hpp"

//! A continuous-dividend-yield curve subtracted from the equity drift; a Curve read
//! as a continuously-compounded yield.
class ContinuousDividendsCurve : public Curve
{
    //!
  public:
    ContinuousDividendsCurve( const string& ObjectNameg );
    ~ContinuousDividendsCurve() override;
};
