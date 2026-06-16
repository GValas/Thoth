#pragma once
#include "asset.hpp"
#include "volatility.hpp"

class Single : public Asset
{

  protected:
    Volatility* _volatility;
    double _spot = 0;

  public:
    // setter
    void SetSpot( double Spot );
    void SetVolatility( Volatility& Volatility );
    void SetToday( const date& Today ) override;

    //! getter
    double GetSpot() override;
    Volatility* GetVolatility(); //<<< a virer
    virtual double GetLocalVolatility( const double Strike,
                                       const date& MaturityDate ) = 0;
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate );
    virtual double GetForward( const date& MaturityDate ) = 0;

    //! mcl node
    virtual MonteCarloNode* GetDriftNode( NodeCollector& NC ) = 0;
    virtual MonteCarloNode* GetNode( NodeCollector& NC );
    MonteCarloNode* GetVolNode( NodeCollector& NC );

    //! build a LocalVolatilityNode that samples the Dupire local-vol surface onto
    //! a per-diffusion-date log-spot grid (used for a local-vol surface like SABR);
    //! SpotNode is the spot path the surface is read along
    LocalVolatilityNode* BuildLocalVolNode( NodeCollector& NC, MonteCarloNode* SpotNode );

    //! constructor & destructor
    Single( const string& ObjectName,
            const string& ObjectKind );
    ~Single() override;
};
