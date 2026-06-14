#pragma once
#include "curve.hpp"

class ContinuousDividendsCurve : public Curve
{
    //!
  public:
    ContinuousDividendsCurve( const string& ObjectNameg );
    ~ContinuousDividendsCurve() override;
};
