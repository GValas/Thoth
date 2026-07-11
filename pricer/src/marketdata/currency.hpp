#pragma once
#include "market_data.hpp"
#include "yield_curve.hpp"

class HullWhite;

//! the correlation-matrix pseudo-single of a currency's Hull-White rate factor is
//! "<currency>_ir" (the same convention as the Heston "<underlying>_var" factor)
inline constexpr char IR_FACTOR_SUFFIX[] = "_ir";

//! currency.hpp — a currency wrapping its yield curve(s).
//!
//! A currency and its curves: `rate` is the projection / funding curve every
//! forward and drift is built on; the optional `discount_rate` is the OIS /
//! collateral curve cash flows are DISCOUNTED on (multi-curve). When absent it
//! defaults to `rate`, reducing exactly to the historic single-curve behaviour.
//! A market-data input — its bumpable risk factor (rho) lives on the wrapped
//! curve(s), so it carries no factor of its own (it does not override
//! ApplyShift/HasFactor; the rho bump is applied on the curves directly).
class Currency : public MarketData
{
  private:
    //! the projection / funding zero-rate curve (non-owning; resolved from the
    //! book by reference). Source of every forward and drift rate.
    YieldCurve* _rate;
    //! the discounting (OIS / collateral) curve; == _rate unless the optional
    //! `discount_rate` field names a separate curve. Source of all discount factors.
    YieldCurve* _discount_rate;
    //! optional Hull-White 1F short-rate model on this currency's discounting
    //! (null = deterministic rates, the historic behaviour)
    HullWhite* _rate_model;

  public:
    //! read own fields (the projection curve, and the optional discounting curve)
    void Configure( ObjectReader& reader ) override;

    //! propagate the valuation date into the wrapped curve(s) (and base Object)
    void SetToday( const date& Today ) override;

    //! getters — projection curve, and the discounting (OIS) curve
    YieldCurve* GetRate() const;
    YieldCurve* GetDiscountRate() const;
    //! true when a distinct discounting curve is configured (multi-curve book)
    bool HasDistinctDiscountCurve() const;

    //! the optional Hull-White model (null = deterministic rates), and the
    //! matrix pseudo-single naming its noise factor ("<name>_ir")
    HullWhite* GetRateModel() const;
    string IrFactorName() const;

    //! mcl nodes — term-structured zero-rate legs: GetRateNode feeds the drift,
    //! GetDiscountRateNode the cash-flow discounting (same node when single-curve)
    MonteCarloNode* GetRateNode( NodeCollector& NC );
    MonteCarloNode* GetDiscountRateNode( NodeCollector& NC );
    //! the Hull-White exponent X(t) = int x + V/2 (builds the OU factor over the
    //! correlated "<name>_ir#noise" on first request; shared across scenarios —
    //! common random numbers, and no Greek bumps the HW parameters)
    MonteCarloNode* GetHwExponentNode( NodeCollector& NC );

    //! constructor, destructor
    Currency( const string& ObjectName );
    ~Currency() override;
};
