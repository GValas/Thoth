#pragma once
#include "underlying.hpp"

//! composite.hpp — composite / quanto underlying.
//!
//! A composite is a foreign-currency asset converted into a settlement currency at
//! the live FX rate: its value is S_eq * FX. Two distinct effects appear depending
//! on which currency the *payoff* settles in:
//!  - composite (payoff = S_eq * FX, paid in the settlement ccy): the converted
//!    asset's vol combines the equity and FX vols with their correlation (see
//!    GetImplicitVol — sqrt of the variance of a product of lognormals).
//!  - quanto (payoff in S_eq units but paid in the settlement ccy at a *fixed* FX):
//!    the asset itself is unchanged but its drift is corrected by rho*sigma_eq*
//!    sigma_fx (see GetForward and the QuantoAdjustmentNode in GetNode).
//! The node graph wires the wrapped underlying, the FX leg and the relevant drift /
//! vol correction together.

//! A composite (quanto) underlying: an asset quoted in one currency but settled in
//! another at the prevailing FX (S*FX). Combines the underlying with the FX leg and
//! a quanto drift correction in the node graph.
class Composite : public Underlying
{

  private:
    //! attributes
    //! the wrapped foreign-currency asset (non-owning). Its own currency is the
    //! "quote" ccy; _currency (from the base) holds the composite/settlement ccy.
    Underlying* _underlying;

  public:
    //! read own fields (wrapped underlying + the composite/settlement currency)
    void Configure( ObjectReader& reader ) override;

    //! setter — set the composite/settlement currency (stored in the base _currency).
    void SetCompoCurrency( Currency& CompositeCurrency );
    //! setter — set the wrapped foreign-currency underlying.
    void SetUnderlying( Underlying& Underlying );
    //! inject correlation; overridden to also propagate it into the wrapped underlying.
    void SetCorrelation( Correlation* Correlation ) override;

    //! getter — the wrapped underlying.
    Underlying* GetUnderlying() const;
    //! getter — the composite/settlement currency (alias of the base _currency).
    Currency* GetCompositeCurrency() const;
    //! single-name set = wrapped underlying's singles plus the FX leg's singles.
    SingleSet GetSingleSet() const override;
    //! currency set = settlement currency plus the wrapped underlying's currency.
    CurrencySet GetCurrencySet() const override;

    //! a single (quanto'd) asset: collapses to one spatial dimension
    bool IsGriddable() const override { return true; }

    //! mcl node
    //! spot node S_eq*FX: product of the quanto-adjusted underlying node and the FX node.
    MonteCarloNode* GetNode( NodeCollector& NC ) override;
    //! composite vol node: combines underlying vol, FX vol and their correlation.
    MonteCarloNode* GetVolNode( NodeCollector& NC ) override;
    //! quanto correlation node between the composite driver and a target FX pair.
    MonteCarloNode* GetCorrelNode( NodeCollector& NC,
                                   const string& UnderlyingCurrency,
                                   const string& BaseCurrency ) override;

    //! fwd & vol
    //! spot in settlement ccy: S_eq * FX(quote->settlement).
    double GetSpot() const override;
    //! forward in QuantoCurrency, with the quanto drift correction when it differs.
    double GetForward( const date& MaturityDate,
                       Currency* QuantoCurrency ) override;
    //! composite implied vol: sqrt(v_eq^2 + v_fx^2 + 2*rho*v_eq*v_fx).
    double GetImplicitVol( const double Strike,
                           const date& MaturityDate ) override;

    //! constructor, destructor — tagged KIND_COMPOSITE.
    Composite( const string& ObjectName );
    ~Composite() override;
};
