#pragma once
#include "market_data.hpp"
#include "node_collector.hpp"

//! curve.hpp — the term-structure interpolation primitive shared by all rate curves.
//!
//! A term-structure curve of (date, value) pillars read as a continuously-compounded
//! rate by linear interpolation between pillars (flat beyond the ends), plus an
//! additive shift for the rho bump. Base of yield / repo / dividend curves.
//!
//! Invariants: _date_list is ascending (validated on set) and has the same length as
//! _value_list; values are stored as decimal rates (the YAML supplies them in percent,
//! scaled by 0.01 on the way in). The single rho risk factor is the only bumpable
//! input, applied as a parallel additive shift on every interpolated value.
class Curve : public MarketData
{
  private:
    //! pillar dates (ascending); the abscissae of the term structure
    vector<date> _date_list;
    //! pillar values aligned with _date_list, stored as decimal continuously-
    //! compounded rates (rate / yield / repo depending on the concrete kind)
    LaVector _value_list;

    //! additive parallel shift added to every curve value; used by the
    //! bump-and-revalue rho (on yield curves). Zero in normal pricing.
    double _curve_shift = 0.0;

  public:
    //! read the (date, value) pillars common to every curve kind (yield / repo /
    //! continuous-dividend). The concrete kinds add no own fields, so they inherit
    //! this directly through MakeConfigurable<T>.
    void Configure( ObjectReader& reader ) override;

    //! setter — store the pillar dates (validated ascending via CheckDateList)
    void SetDateList( const vector<date>& DateList );
    //! setter — adopt the pillar values and rescale percent -> decimal (x 0.01)
    void SetValueList( la_vector* ValueList );

    //! set the parallel shift (rho bump, in decimal rate); 0 restores the curve
    void SetCurveShift( double Shift ) { _curve_shift = Shift; }

    //! MarketData bump-and-revalue: a curve responds only to the "rate" factor (rho)
    void ApplyShift( const string& Factor, double Shift ) override
    {
        if ( Factor == RISK_FACTOR_RATE )
            SetCurveShift( Shift );
    }
    [[nodiscard]] bool HasFactor( const string& Factor ) const override
    {
        return Factor == RISK_FACTOR_RATE;
    }

    //! getter

    //! mcl node — a YieldCurveNode that samples this curve's zero rate at every
    //! diffusion date, so the simulated drift follows the whole term structure
    MonteCarloNode* GetNode( NodeCollector& NC );

    //! curve interpolated value — the continuously-compounded rate at Maturity, with
    //! the rho shift applied (see GetCurveValue in the .cpp for the interpolation rule)
    double GetCurveValue( const date& Maturity ) const;

    //! constructor, destructor
    Curve( const string& ObjectName,
           const string& ObjectKind );
    ~Curve( void );
};
