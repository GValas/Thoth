#pragma once
#include "market_data.hpp"
#include "node_collector.hpp"

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
    //! setter
    void SetDateList( const vector<date>& DateList );
    void SetValueList( la_vector* ValueList );

    //! set the parallel shift (rho bump, in decimal rate); 0 restores the curve
    void SetCurveShift( double Shift ) { _curve_shift = Shift; }

    //! getter

    //! mcl node
    MonteCarloNode* GetNode( NodeCollector& NC );

    //! curve interpolated value
    double GetCurveValue( const date& Maturity );

    //! constructor, destructor
    Curve( const string& ObjectName,
           const string& ObjectKind );
    ~Curve( void );
};
