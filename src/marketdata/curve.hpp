#pragma once
#include "market_data.hpp"
#include "node_collector.hpp"

//! A term-structure curve of (date, value) pillars read as a continuously-compounded
//! rate by linear interpolation between pillars (flat beyond the ends), plus an
//! additive shift for the rho bump. Base of yield / repo / dividend curves.
class Curve : public MarketData
{
  private:
    //! attributes
    vector<date> _date_list;
    LaVector _value_list;

    //! additive parallel shift added to every curve value; used by the
    //! bump-and-revalue rho (on yield curves). Zero in normal pricing.
    double _curve_shift = 0.0;

  public:
    //! read the (date, value) pillars common to every curve kind (yield / repo /
    //! continuous-dividend). The concrete kinds add no own fields, so they inherit
    //! this directly through MakeConfigurable<T>.
    void Configure( ObjectReader& reader ) override;

    //! setter
    void SetDateList( const vector<date>& DateList );
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

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC );

    //! curve interpolated value
    double GetCurveValue( const date& Maturity ) const;

    //! constructor, destructor
    Curve( const string& ObjectName,
           const string& ObjectKind );
    ~Curve( void );
};
