#pragma once
#include "curve.hpp"

class RepoCurve : public Curve
{

  public:
    RepoCurve( const string& ObjectName );
    ~RepoCurve() override;
};
