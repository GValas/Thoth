#pragma once
#include "asset.hpp"
#include "object_sets.hpp"

//! underlying.hpp — the diffusable-underlying abstraction.
//!
//! Defines Underlying, the common base of every priceable underlying shape in the
//! engine: a plain single name (mono), a composite/quanto (Composite), a weighted
//! basket (AbsoluteBasket via Basket) and a best/worst-of rainbow (Rainbow). The
//! base exposes the quantities every pricing engine needs — forward, implied vol,
//! the single-name and currency sets it touches, and the Monte-Carlo node graph
//! (spot / vol / correlation nodes) — plus capability predicates that let the
//! engines pick a method by polymorphism instead of RTTI / kind-string tests.

//! Underlying only stores a Correlation* (the quanto matrix) — a forward declaration
//! keeps this header out of the correlation -> forex -> single include cycle (Single
//! now derives from Underlying). The .cpp files that call Correlation methods include
//! correlation.hpp directly.
class Correlation;

//! Abstract diffusable underlying of a contract (mono / composite / basket /
//! rainbow): exposes the forward, implied vol and its single-name & currency sets,
//! and builds the MCL spot / vol / correlation nodes.
class Underlying : public Asset
{

    //
  protected:
    //! the global correlation/FX matrix (quanto & basket cross-correlations). Shared,
    //! not owned; injected via SetCorrelation and forwarded down to any sub-underlyings.
    Correlation* _correlation; // quanto stuffs

    //
  public:
    //! setter
    // void SetSpot( const double Spot );
    // void SetCurrency( Currency * Currency );
    //! inject the correlation matrix. Composite/Basket override to propagate it to
    //! their wrapped/member underlyings before storing it here.
    virtual void SetCorrelation( Correlation* Correlation );

    //! fwd & vol
    //! forward of the underlying for delivery at MaturityDate, expressed in
    //! QuantoCurrency (a quanto drift correction is applied when QuantoCurrency
    //! differs from the underlying's own currency). Pure virtual.
    virtual double GetForward( const date& MaturityDate,
                               Currency* QuantoCurrency ) = 0;
    //! Black-Scholes-equivalent implied vol at the given Strike and MaturityDate.
    //! For multi-asset shapes this is the moment-matched basket vol. A non-positive
    //! Strike conventionally means "representative ATM vol" (used for grid setup).
    //! Pure virtual.
    virtual double GetImplicitVol( const double Strike,
                                   const date& MaturityDate ) = 0;

    //! singles & ccys
    // virtual set<string> GetSingleNameList() = 0;
    // virtual set<string> GetCurrencyNameList() = 0;
    //! the set of single-name (equity/FX) factors this underlying ultimately depends
    //! on — used to assemble the simulation universe and the correlation sub-matrix.
    virtual SingleSet GetSingleSet() const = 0;
    //! the set of currencies this underlying touches (own + settlement/component
    //! currencies) — drives the rates/FX needed for diffusion and discounting.
    virtual CurrencySet GetCurrencySet() const = 0;

    //! getter
    // Currency * GetCurrency();
    //! the injected correlation matrix (may be nullptr before SetCorrelation runs).
    Correlation* GetCorrelation() const;

    //! capability predicates (replace engine/contract RTTI & kind-string tests
    //! with polymorphism). Defaults below; concrete underlyings override.

    //! the underlying collapses to a single spatial dimension, so a 1-D PDE grid
    //! and the moment-matched closed form apply (equity, composite, basket). Not
    //! a rainbow (its payoff orders the assets, needing their joint law) nor a
    //! bare FX leg.
    [[nodiscard]] virtual bool IsGriddable() const { return false; }

    //! a plain single-name underlying (a mono): the only shape the MCL single-tree
    //! Greeks can isolate a per-contract spot bump within a shared path.
    [[nodiscard]] virtual bool IsMono() const { return false; }

    //! the spot the deterministic engines (PDE) and the MCL diffusion start from.
    //! Defaults to the plain spot; a mono equity with discrete dividends returns the
    //! escrowed spot (spot minus the PV of dividends due up to LastDate), so all
    //! engines price the same escrowed forward. See Single/Equity::GetDiffusionSpot.
    [[nodiscard]] virtual double GetDiffusionSpot( const date& /*LastDate*/ ) const { return GetSpot(); }

    //! continuous carry yield (dividend yield + repo) the PDE subtracts from the
    //! rate; 0 by default, a mono equity returns its single's div+repo.
    [[nodiscard]] virtual double DividendRepoYield( const date& /*MaturityDate*/ ) const { return 0; }

    //! escrowed-dividend model: PV (as of AsOf) of the discrete cash dividends with
    //! ex-date after AsOf. Added to the escrowed grid value to recover the observed
    //! spot for the PDE early-exercise test (matching the MCL dividend node). 0 by
    //! default; a mono equity returns its single's future-dividend PV.
    [[nodiscard]] virtual double FutureDividendPv( const date& /*AsOf*/ ) const { return 0; }

    //! mcl node
    //! build (or fetch from the collector's cache) the Monte-Carlo node producing
    //! this underlying's spot path. NC de-duplicates shared sub-nodes by name.
    virtual MonteCarloNode* GetNode( NodeCollector& NC ) = 0;
    //! the node producing this underlying's instantaneous (stochastic) vol path —
    //! needed by quanto/composite drift corrections. Not defined for every shape.
    virtual MonteCarloNode* GetVolNode( NodeCollector& NC ) = 0;
    //! the node producing the correlation between this underlying's driver and the
    //! FX pair UnderlyingCurrency/BaseCurrency — the quanto correlation input.
    virtual MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                           const string& UnderlyingCurrency,
                                           const string& BaseCurrency ) = 0;

    //! construct with the object's instance name and its concrete kind tag (e.g.
    //! KIND_BASKET / KIND_COMPOSITE / KIND_RAINBOW), forwarded to Asset.
    Underlying( const string& ObjectName,
                const string& ObjectKind );
    ~Underlying() override;
};
