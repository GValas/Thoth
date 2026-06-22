#pragma once
#include "curve.hpp"

//! continuous_dividends_curve.hpp — the continuous dividend-yield term structure.
//!
//! A continuous-dividend-yield curve subtracted from the equity drift; a Curve read
//! as a continuously-compounded yield q(T). It is a pure tag over Curve: it adds no
//! own data or behaviour, only a distinct kind so the equity can resolve it by role
//! (carry yield) and so the bump machinery treats it separately from the rate curve.
class ContinuousDividendsCurve : public Curve
{
    //! no own fields — the (date, value) pillars and interpolation are inherited
  public:
    //! constructor — tags the underlying Curve with the continuous-dividend kind
    ContinuousDividendsCurve( const string& ObjectNameg );
    ~ContinuousDividendsCurve() override;
};
