#pragma once
#include "object.hpp"

//! risk-factor names for the bump-and-revalue Greeks (MarketData::ApplyShift). Model
//! parameters (a Heston/SABR alpha, kappa, ...) are passed as their own plain name.
inline constexpr char RISK_FACTOR_VOL[] = "vol";   //!< parallel volatility bump (vega)
inline constexpr char RISK_FACTOR_RATE[] = "rate"; //!< parallel yield-curve bump (rho)

//! Base class for a market-data object (curve, volatility, correlation, currency,
//! ...): a named Object the engines read. Beyond the kind tag it carries the
//! bump-and-revalue contract — an additive shift on a named risk factor — so the
//! pricer bumps every market input polymorphically instead of reaching into each
//! concrete setter. Concrete kinds add the actual data and accessors.
class MarketData : public Object
{

  public:
    //! apply an additive shift on a named risk factor (0 restores); a no-op for a
    //! factor this kind does not carry, so one polymorphic call bumps exactly the
    //! inputs that respond. The shift is read back in the kind's own accessors.
    //! Default: carries no factor (e.g. Correlation / Currency today).
    virtual void ApplyShift( const string& /*Factor*/, double /*Shift*/ ) {}

    //! does this object respond to the named risk factor? Lets a Greek skip a
    //! parameter no input in the book carries. Default: no.
    [[nodiscard]] virtual bool HasFactor( const string& /*Factor*/ ) const { return false; }

    MarketData( const string& ObjectName,
                const string& ObjectKind );
    ~MarketData() override;
};
