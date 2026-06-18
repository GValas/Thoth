#pragma once
#include "asset.hpp"
#include "correlation.hpp"

//! Abstract diffusable underlying of a contract (mono / composite / basket /
//! rainbow): exposes the forward, implied vol and its single-name & currency sets,
//! and builds the MCL spot / vol / correlation nodes.
class Underlying : public Asset
{

    //
  protected:
    Correlation* _correlation; // quanto stuffs

    //
  public:
    //! setter
    // void SetSpot( const double Spot );
    // void SetCurrency( Currency * Currency );
    virtual void SetCorrelation( Correlation* Correlation );

    //! fwd & vol
    virtual double GetForward( const date& MaturityDate,
                               Currency* QuantoCurrency ) = 0;
    virtual double GetImplicitVol( const double Strike,
                                   const date& MaturityDate ) = 0;

    //! singles & ccys
    // virtual set<string> GetSingleNameList() = 0;
    // virtual set<string> GetCurrencyNameList() = 0;
    virtual SingleSet GetSingleSet() const = 0;
    virtual CurrencySet GetCurrencySet() const = 0;

    //! getter
    // Currency * GetCurrency();
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
    virtual MonteCarloNode* GetNode( NodeCollector& NC ) = 0;
    virtual MonteCarloNode* GetVolNode( NodeCollector& NC ) = 0;
    virtual MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                           const string& UnderlyingCurrency,
                                           const string& BaseCurrency ) = 0;

    //!
    Underlying( const string& ObjectName,
                const string& ObjectKind );
    ~Underlying() override;
};
