#pragma once
#include "volatility.hpp"

class BsVolatility : public Volatility
{

  private:
    //! object attributes
    double _volatility;

  public:
    //! setter
    void SetVolatility( double BsVolatility );

    //!
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC ) override;

    //! constructor & destructor
    BsVolatility( const string& ObjectName );
    ~BsVolatility() override;
};
