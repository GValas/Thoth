#include "thoth.hpp"
#include "curve.hpp"

//!
Curve::Curve( const string& ObjectName,
              const string& ObjectKind ) : MarketData( ObjectName, ObjectKind )
{
}

//! _value_list is owned by its LaVector wrapper; nothing to free by hand
Curve::~Curve() = default;

//! setter
void Curve::SetDateList( const vector<date>& DateList )
{
    CheckDateList( DateList );
    _date_list = DateList;
}

//! setter
void Curve::SetValueList( la_vector* ValueList )
{
    _value_list = ValueList;
    la_vector_scale( _value_list, 0.01 );
}

//! continuously-compounded zero rate at MaturityDate: linear interpolation on the
//! rate between the bracketing pillars (ACT/365 weight), flat extrapolation beyond
//! the first/last pillar. A flat input curve (equal pillar values) returns that
//! constant. The additive _curve_shift (rho bump) is applied last.
double Curve::GetCurveValue( const date& MaturityDate ) const
{
    const size_t n = _date_list.size();

    double r;
    if ( n <= 1 || MaturityDate <= _date_list.front() )
    {
        r = la_vector_get( _value_list, 0 ); //!< single pillar or before the curve
    }
    else if ( MaturityDate >= _date_list.back() )
    {
        r = la_vector_get( _value_list, n - 1 ); //!< after the curve
    }
    else
    {
        //! bracket: largest i with _date_list[i] <= MaturityDate < _date_list[i+1]
        size_t i = 0;
        while ( i + 1 < n && _date_list[i + 1] <= MaturityDate )
        {
            i++;
        }
        const double r0 = la_vector_get( _value_list, i );
        const double r1 = la_vector_get( _value_list, i + 1 );
        const double w = YearFraction( _date_list[i], MaturityDate ) / YearFraction( _date_list[i], _date_list[i + 1] );
        r = r0 + w * ( r1 - r0 );
    }

    return r + _curve_shift;
}

//! MCL drives a single constant rate over the whole diffusion (no term structure
//! on the simulation path yet): use the front-pillar rate, matching the historical
//! flat behaviour. Term-structured MCL paths are a separate, larger change.
MonteCarloNode* Curve::GetNode( NodeCollector& NC )
{

    return NC.GetOrCreate<ConstantNode>( _name,
                                         [&]( ConstantNode* Y )
                                         { Y->SetConstantValue( GetCurveValue( _date_list.front() ) ); } );
}