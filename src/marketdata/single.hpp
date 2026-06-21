#pragma once
#include "underlying.hpp"
#include "volatility.hpp"

//! A single tradable name (base of Equity / Forex): an Asset with a spot and a
//! volatility surface. Provides the implied / Dupire local vol and builds the MCL
//! spot-diffusion node (constant-vol, or a local-vol grid for a SABR surface).
//!
//! A single name IS a single-asset underlying, so Single derives from Underlying and
//! implements its contract-diffusion interface (quanto forward, single/currency sets,
//! correlation node) directly — there is no separate Mono adapter. The multi-asset
//! underlyings (Composite / Basket / Rainbow) are the other Underlying shapes.
class Single : public Underlying
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
    double GetSpot() const override;

    //! the spot the MCL diffusion starts from. Defaults to the plain spot; an
    //! equity with discrete dividends overrides it with the escrowed spot (so the
    //! diffused path matches the escrowed forward up to the last diffusion date).
    virtual double GetDiffusionSpot( const date& /*LastDate*/ ) const { return _spot; }

    //! continuous carry yield (dividend yield + repo) subtracted from the rate in
    //! the drift. 0 by default; an equity overrides it. Lets the deterministic
    //! engines (ANA/PDE) subtract the same div+repo the MCL drift node does.
    virtual double DividendRepoYield( const date& /*MaturityDate*/ ) const { return 0; }

    //! escrowed-dividend model: PV (as of AsOf) of the discrete cash dividends due
    //! after AsOf. 0 by default; an equity with discrete dividends overrides it. Lets
    //! the PDE recover the observed spot (escrowed value + this) for early exercise.
    virtual double FutureDividendPv( const date& /*AsOf*/ ) const { return 0; }

    Volatility* GetVolatility() const;
    virtual double GetLocalVolatility( const double Strike,
                                       const date& MaturityDate ) = 0;
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //! the plain (non-quanto) forward, supplied by the concrete name (Equity / Forex)
    virtual double GetForward( const date& MaturityDate ) const = 0;

    //! an FX leg (rather than an equity name): the MCL correlation/Cholesky split
    //! groups FX singles separately. Default false; Forex overrides.
    [[nodiscard]] virtual bool IsForex() const { return false; }

    //! --- Underlying (contract-diffusion) role, formerly the Mono adapter ---

    //! quanto-aware forward: the plain forward times the quanto drift correction when
    //! the payoff settles in a currency other than this asset's (mirrors the MCL
    //! QuantoAdjustmentNode so ANA / PDE / MCL agree).
    double GetForward( const date& MaturityDate,
                       Currency* QuantoCurrency ) override;

    //! a single name spans exactly itself / its own currency
    SingleSet GetSingleSet() const override;
    CurrencySet GetCurrencySet() const override;

    //! correlation node for this single name (resolved from the global matrix)
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency,
                                   const string& BaseCurrency ) override;

    //! a single griddable name (an FX leg is not griddable); the mono shape the MCL
    //! single-tree Greeks can isolate a per-contract spot bump within
    [[nodiscard]] bool IsGriddable() const override { return !IsForex(); }
    [[nodiscard]] bool IsMono() const override { return true; }

    //! mcl node
    virtual MonteCarloNode* GetDriftNode( NodeCollector& NC ) = 0;

    //! optional discrete-dividend (escrow) node wired into the spot diffusion;
    //! null by default (no dividends). An equity with a discrete-dividend schedule
    //! overrides it.
    virtual MonteCarloNode* GetDividendNode( NodeCollector& /*NC*/ ) { return nullptr; }

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
