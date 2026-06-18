#pragma once
#include "continuous_dividends_curve.hpp"
#include "discrete_dividends.hpp"
#include "node_collector.hpp"
#include "repo_curve.hpp"
#include "single.hpp"

//! A single equity underlying: spot + volatility in its currency, with optional repo,
//! continuous-dividend and discrete cash-dividend schedules. Provides the forward,
//! the implied and Dupire local vol, and builds the MCL spot-diffusion / drift nodes.
class Equity : public Single
{
  private:
    //! attributes
    RepoCurve* _repo;
    ContinuousDividendsCurve* _continuous_dividends;
    DiscreteDividends* _discrete_dividends = nullptr;

    //! escrowed-dividend model: present value (discounted on this equity's currency
    //! curve) of the discrete cash dividends with ex-date in (today, UpTo].
    double DiscreteDividendsPv( const date& UpTo ) const;

  public:
    //! setter
    void SetRepo( RepoCurve* Repo );
    void SetContinuousDividends( ContinuousDividendsCurve* ContinuousDividends );
    void SetDiscreteDividends( DiscreteDividends* DiscreteDividends );

    //! getter
    RepoCurve* GetRepo();
    ContinuousDividendsCurve* GetContinuousDividends();
    double GetSpot() const override;

    //! escrowed spot for the MCL diffusion: spot minus the PV of the discrete
    //! dividends due up to the last diffusion date, so the diffused path grows to
    //! the same (escrowed) forward the analytic / PDE engines use.
    double GetDiffusionSpot( const date& LastDate ) const override;

    //! continuous dividend yield + repo spread (the carry yield)
    double DividendRepoYield( const date& MaturityDate ) const override;

    //! local vol
    bool UseLocalVol();

    //! forward
    double GetForward( const date& MaturityDate ) const override;

    //! implicit vol
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate );

    double GetLocalVolatility( const double Strike,
                               const date& MaturityDate ) override;

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    MonteCarloNode* GetDriftNode( NodeCollector& NC ) override;
    MonteCarloNode* GetDividendNode( NodeCollector& NC ) override; //!< escrow node when discrete divs present

    // constructor
    Equity( const string& ObjectName );
    ~Equity() override;
};
