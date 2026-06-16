#pragma once
#include "continuous_dividends_curve.hpp"
#include "node_collector.hpp"
#include "repo_curve.hpp"
#include "single.hpp"

//! A single equity underlying: spot + volatility in its currency, with optional repo
//! and continuous-dividend curves. Provides the forward, the implied and Dupire
//! local vol, and builds the MCL spot-diffusion / drift nodes.
class Equity : public Single
{
  private:
    //! attributes
    RepoCurve* _repo;
    ContinuousDividendsCurve* _continuous_dividends;

  public:
    //! setter
    void SetRepo( RepoCurve* Repo );
    void SetContinuousDividends( ContinuousDividendsCurve* ContinuousDividends );

    //! getter
    RepoCurve* GetRepo();
    ContinuousDividendsCurve* GetContinuousDividends();
    double GetSpot() override;

    //! local vol
    bool UseLocalVol();

    // discrete_dividends

    //! forward
    double GetForward( const date& MaturityDate ) override;

    //! implicit vol
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate );

    double GetLocalVolatility( const double Strike,
                               const date& MaturityDate ) override;

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    MonteCarloNode* GetDriftNode( NodeCollector& NC ) override;

    // constructor
    Equity( const string& ObjectName );
    ~Equity() override;
};
