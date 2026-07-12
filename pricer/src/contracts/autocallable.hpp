#pragma once
#include "contract.hpp"

//! An autocallable note (Athena or Phoenix flavour) on one underlying.
//!
//! Schedule: explicit `autocall_dates` strictly between today and maturity.
//!
//! ATHENA (no `coupon_barrier`): at the first observation with S >= autocall
//! level the note redeems early and pays nominal * (1 + k * coupon), k the
//! 1-based observation count (the "snowball" accumulated coupon). If it
//! survives to maturity (observation n+1): S_T >= autocall level pays
//! nominal * (1 + (n+1) * coupon); above the protection level it returns the
//! bare nominal; below, the capital is at risk linearly — nominal * S_T / S_ref
//! (the short down-and-in put, cash-settled).
//!
//! PHOENIX (`coupon_barrier` set, conventionally below the autocall level): the
//! coupon detaches PER PERIOD — at every observation (and at maturity) where
//! the note is still alive and S >= coupon level, the flow coupon * nominal is
//! PAID AT THAT DATE (its own discounting; pathwise under Hull-White). The
//! early redemption pays the bare nominal (the date's coupon flow rides along,
//! the coupon condition being implied by S >= autocall >= coupon level); the
//! maturity redemption is nominal above the protection level / linear below,
//! plus the terminal coupon flow. With `coupon_memory`, a paying observation
//! also recovers every consecutively missed coupon since the last payment.
//!
//! Levels are booked in PERCENT of the underlying's spot (the rebased 100 for a
//! basket) and resolved once in SetToday against the unbumped spot — the same
//! sticky-cash convention as relative strikes, so every Greek bumps the spot
//! and never the levels, and the PDE grid Greeks agree with the MCL bumps.
//!
//! Engine support: MCL (one flow node per schedule date — the autocall is an
//! automatic trigger, not an optimal exercise, so no LSM is needed and the
//! pathwise discounting composes with multi-curve and Hull-White books); PDE
//! (backward induction, the layer overwritten with the accrued rebate above the
//! autocall level at each observation step); ANA rejects (no closed form).
class Autocallable : public Contract
{

  private:
    date _maturity_date;
    vector<date> _autocall_dates; //!< sorted, strictly between today and maturity
    double _autocall_pct = 0;     //!< autocall level input, percent of spot
    double _protection_pct = 0;   //!< protection level input, percent of spot
    double _coupon = 0;           //!< per-observation coupon (decimal; input in percent)
    double _nominal = 100;        //!< redemption notional
    //! Phoenix flavour: per-period conditional coupon barrier (percent; absent =
    //! Athena snowball) and the optional missed-coupon memory
    double _coupon_barrier_pct = 0;
    bool _is_phoenix = false;
    bool _coupon_memory = false;

    //! cash levels, resolved once in SetToday against the unbumped spot
    double _autocall_level = 0;
    double _protection_level = 0;
    double _coupon_level = 0; //!< Phoenix coupon trigger (0 in Athena mode)
    double _reference_spot = 0;

  public:
    //! read own fields (maturity, autocall_dates, autocall_barrier,
    //! protection_barrier, coupon, nominal), then the common contract attributes
    void Configure( ObjectReader& reader ) override;
    //! resolve the percent levels into cash against the valuation-date spot
    //! (idempotent under the theta roll; never re-anchored by a Greek bump)
    void SetToday( const date& Today ) override;

    //! getters (cash levels, for the PDE grid and the flow nodes)
    date GetMaturityDate() const override;
    const vector<date>& GetAutocallDates() const { return _autocall_dates; }
    double AutocallLevel() const { return _autocall_level; }
    double ProtectionLevel() const { return _protection_level; }
    double ReferenceSpot() const { return _reference_spot; }
    double GetNominal() const { return _nominal; }
    //! Phoenix flavour accessors (coupon level 0 / flags false in Athena mode)
    bool IsPhoenix() const { return _is_phoenix; }
    bool HasCouponMemory() const { return _coupon_memory; }
    double CouponLevel() const { return _coupon_level; }
    double PeriodCoupon() const { return _nominal * _coupon; } //!< one period's cash coupon
    //! early-redemption amount at the k-th observation (1-based): the Athena
    //! snowball N * (1 + k * c); a Phoenix redeems at N + the period coupon
    //! (its coupon flows detach at their own dates)
    double Rebate( size_t Position ) const;

    //! the maturity redemption profile (also the PDE terminal condition and its
    //! far boundaries): accrued coupon above the autocall level, nominal above
    //! the protection level, linear capital loss below
    double Intrinsic( const double Spot ) override;
    //! the autocall is an automatic trigger, not an optimal exercise
    bool IsAmerican() override;

    //! mcl nodes: one flow per schedule date (autocall dates + maturity)
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! dates: spot observations = payment dates = autocall dates + maturity
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;

    //! constructor / destructor
    Autocallable( const string& ObjectName );
    ~Autocallable() override;
};
