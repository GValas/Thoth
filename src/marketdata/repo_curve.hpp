#pragma once
#include "curve.hpp"

//! repo_curve.hpp — the repo (borrow) rate term structure.
//!
//! A repo-rate curve (a borrow cost subtracted from the equity drift); a Curve read
//! as a continuously-compounded rate. Like ContinuousDividendsCurve it is a pure kind
//! tag over Curve: the equity adds the repo rate to the dividend yield to form the
//! total carry it subtracts from the rate in the drift / forward.
class RepoCurve : public Curve
{

  public:
    //! constructor — tags the Curve with the repo-curve kind (no own data)
    RepoCurve( const string& ObjectName );
    ~RepoCurve() override;
};
