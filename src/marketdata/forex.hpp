#pragma once
#include "currency.hpp"
#include "single.hpp"
#include "volatility.hpp"

//! forex.hpp — the FX-pair underlying.
//!
//! An FX pair (underlying currency vs base currency): the spot rate and its (flat)
//! volatility, used for quanto / composite FX conversion in the node graph. The base
//! currency is stored as the Asset's _currency; the underlying (foreign) currency is
//! held here. The drift is the domestic-minus-foreign rate differential (covered
//! interest parity), so the FX diffusion uses both currencies' rate nodes.
class Forex : public Single
{

    //
  private:
    //! the foreign / underlying currency of the pair (the base currency lives in
    //! Asset::_currency); non-owning reference into the book graph
    Currency* _underlying_currency;

  public:
    //! read own fields (base/underlying currencies, optional vol + spot)
    void Configure( ObjectReader& reader ) override;

    //! setter — bind the underlying (foreign) currency
    void SetUnderlyingCurrency( Currency* UnderlyingCurrency );
    //! setter — bind the base (domestic) currency (stored as Asset::_currency)
    void SetBaseCurrency( Currency* BaseCurrency );

    //! getter — this underlying is an FX leg (drives the MCL correlation/Cholesky split)
    bool IsForex() const override { return true; }
    //! getter — base (domestic) currency
    Currency* GetBaseCurrency() const;
    //! getter — underlying (foreign) currency
    Currency* GetUnderlyingCurrency() const;
    //! FX forward (not implemented — errors; see the .cpp)
    double GetForward( const date& MaturityDate ) const override;
    //! the flat FX volatility level (no strike/maturity dependence)
    double GetConstantVol() const;
    //! local vol — for a flat FX surface this is just the constant vol
    double GetLocalVolatility( const double Strike,
                               const date& MaturityDate ) override;

    //! mcl node — FX drift node fed by the domestic and foreign rate nodes (the rate
    //! differential of covered interest parity)
    MonteCarloNode* GetDriftNode( NodeCollector& NC ) override;

    //! constructor
    Forex( const string& ObjectName );
    ~Forex() override;
};
