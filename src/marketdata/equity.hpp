#pragma once
#include "continuous_dividends_curve.hpp"
#include "discrete_dividends.hpp"
#include "node_collector.hpp"
#include "repo_curve.hpp"
#include "single.hpp"

//! equity.hpp — the equity underlying and its carry/dividend machinery.
//!
//! A single equity underlying: spot + volatility in its currency, with optional repo,
//! continuous-dividend and discrete cash-dividend schedules. Provides the forward,
//! the implied and Dupire local vol, and builds the MCL spot-diffusion / drift nodes.
//!
//! Carry model: the forward grows the spot at (rate - continuous_div - repo) and nets
//! off the present value of discrete cash dividends (escrowed-dividend model). The same
//! carry yield and the same escrow PV feed the ANA forward, the PDE carry term and the
//! MCL drift/dividend nodes, so all three engines price a consistent forward.
class Equity : public Single
{
  private:
    //! repo (borrow) rate curve, optional (null when absent) — adds to the carry yield
    RepoCurve* _repo;
    //! continuous dividend-yield curve, optional — the q(T) in the carry
    ContinuousDividendsCurve* _continuous_dividends;
    //! discrete cash-dividend schedule, optional — escrowed off the spot/forward
    DiscreteDividends* _discrete_dividends = nullptr;

    //! escrowed-dividend model: present value (discounted on this equity's currency
    //! curve) of the discrete cash dividends with ex-date in (today, UpTo].
    double DiscreteDividendsPv( const date& UpTo ) const;

  public:
    //! read own fields (spot, volatility, currency, optional repo / continuous /
    //! discrete dividends)
    void Configure( ObjectReader& reader ) override;

    //! setter — bind the optional repo curve (null leaves the repo carry at 0)
    void SetRepo( RepoCurve* Repo );
    //! setter — bind the optional continuous dividend-yield curve
    void SetContinuousDividends( ContinuousDividendsCurve* ContinuousDividends );
    //! setter — bind the optional discrete cash-dividend schedule
    void SetDiscreteDividends( DiscreteDividends* DiscreteDividends );

    //! getter — the repo curve (may be null)
    RepoCurve* GetRepo();
    //! getter — the continuous dividend-yield curve (may be null)
    ContinuousDividendsCurve* GetContinuousDividends();
    //! getter — current spot price (delegates to Single)
    double GetSpot() const override;

    //! escrowed spot for the MCL diffusion: spot minus the PV of the discrete
    //! dividends due up to the last diffusion date, so the diffused path grows to
    //! the same (escrowed) forward the analytic / PDE engines use.
    double GetDiffusionSpot( const date& LastDate ) const override;

    //! continuous dividend yield + repo spread (the carry yield)
    double DividendRepoYield( const date& MaturityDate ) const override;

    //! escrowed-dividend model: PV (as of AsOf) of the discrete cash dividends due
    //! after AsOf, on this equity's curve (matches the MCL escrow node). Added to the
    //! escrowed grid value to recover the observed spot for PDE early exercise.
    double FutureDividendPv( const date& AsOf ) const override;

    //! local vol — true when the bound volatility is a local (Dupire/SABR) surface
    bool UseLocalVol();

    //! forward — escrowed-dividend forward: (spot - PV discrete divs) grown at the net
    //! carry (rate - continuous div - repo), all consistent with ANA/PDE/MCL
    double GetForward( const date& MaturityDate ) const override;

    //! implicit vol — the implied vol at (Strike, MaturityDate); delegates to Single,
    //! which feeds the forward to a forward-measure (SABR) surface
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate );

    //! Dupire local vol at (Strike, MaturityDate), evaluated with this equity's spot,
    //! rate r and carry yield q (= repo + continuous dividends, each optional)
    double GetLocalVolatility( const double Strike,
                               const date& MaturityDate ) override;

    //! mcl node — the spot-diffusion node (delegates to Single)
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    MonteCarloNode* GetDriftNode( NodeCollector& NC ) override;
    MonteCarloNode* GetDividendNode( NodeCollector& NC ) override; //!< escrow node when discrete divs present

    // constructor
    Equity( const string& ObjectName );
    ~Equity() override;
};
