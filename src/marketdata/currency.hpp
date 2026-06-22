#pragma once
#include "market_data.hpp"
#include "yield_curve.hpp"

//! currency.hpp — a currency wrapping its discount/yield curve.
//!
//! A currency and its discount (yield) curve: provides discount factors / rates and
//! the MCL rate / discount-factor nodes. A market-data input — its bumpable risk
//! factor (rho) lives on the wrapped curve, so it carries no factor of its own (it
//! does not override ApplyShift/HasFactor; the rho bump is applied on _rate directly).
class Currency : public MarketData
{
  private:
    //! the discount / zero-rate curve for this currency (non-owning; resolved from the
    //! book by reference). Source of all discount factors and drift rates.
    YieldCurve* _rate;

  public:
    //! read own field (the discount/yield curve referenced by name)
    void Configure( ObjectReader& reader ) override;

    //! setter — bind the discount/yield curve (by address, not owned)
    void SetRate( YieldCurve& Rate );
    //! propagate the valuation date into the wrapped curve (and base Object)
    void SetToday( const date& Today ) override;

    //! getter — the wrapped discount/yield curve
    YieldCurve* GetRate() const;

    //! mcl node — discount-factor leg (not implemented / currently unused)
    MonteCarloNode* GetDiscFactorNode( NodeCollector& NC );
    //! mcl node — term-structured zero-rate leg feeding the drift and discounting
    MonteCarloNode* GetRateNode( NodeCollector& NC );

    //! constructor, destructor
    Currency( const string& ObjectName );
    ~Currency() override;
};
