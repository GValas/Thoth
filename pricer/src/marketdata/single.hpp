#pragma once
#include "underlying.hpp"
#include "volatility.hpp"
#include "leverage_surface.hpp"

//! single.hpp — the single-name underlying (base of Equity / Forex).
//!
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
    //! the volatility surface (flat BS, local/SABR, or stochastic/Heston); non-owning
    Volatility* _volatility;
    //! current spot price of the name
    double _spot = 0;

  public:
    // setter
    //! setter — the spot price
    void SetSpot( double Spot );
    //! setter — bind the volatility surface (by address)
    void SetVolatility( Volatility* Volatility );
    //! propagate the valuation date into the vol surface, then up the Asset chain
    void SetToday( const date& Today ) override;

    //! getter — current spot price
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

    //! getter — the bound volatility surface
    Volatility* GetVolatility() const;
    //! Dupire local vol at (Strike, MaturityDate); supplied by the concrete name
    //! (Equity reconstructs r and the carry yield; Forex returns its flat vol)
    virtual double GetLocalVolatility( const double Strike,
                                       const date& MaturityDate ) = 0;
    //! implied vol at (Strike, MaturityDate); feeds the name's forward to the surface
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

    //! build the spot-diffusion node (GBM, Heston, or local-vol grid; wires drift,
    //! noise, dividends and vol). The top-level MCL node for this name.
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    //! a scalar vol node for the callers that need a single representative level
    //! (quanto correction, composite vol/correl); an ATM constant for a local surface
    MonteCarloNode* GetVolNode( NodeCollector& NC ) override;

    //! build a LocalVolatilityNode that samples the Dupire local-vol surface onto
    //! a per-diffusion-date log-spot grid (used for a local-vol surface like SABR);
    //! SpotNode is the spot path the surface is read along
    LocalVolatilityNode* BuildLocalVolNode( NodeCollector& NC, MonteCarloNode* SpotNode );

    //! LSV leverage calibration (binned particle method): simulate the leveraged
    //! Heston diffusion forward along Dates, matching at each date
    //! L^2(s,t) * E[v_t | S_t = s] = sigma_dupire^2(s,t) on the target surface.
    //! One leverage layer per date, same log-spot grid layout as the local-vol node.
    LeverageSurface CalibrateLeverage( const vector<date>& Dates );

    //! build the MCL leverage node (a per-date grid read at the previous spot, the
    //! same mechanics as the Dupire local-vol node) from a fresh calibration on the
    //! collector's diffusion dates; SpotNode is the spot path it is read along
    LocalVolatilityNode* BuildLeverageNode( NodeCollector& NC, MonteCarloNode* SpotNode );

    //! constructor & destructor
    Single( const string& ObjectName,
            const string& ObjectKind );
    ~Single() override;
};
