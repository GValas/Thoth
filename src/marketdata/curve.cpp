#include "thoth.hpp"
#include "curve.hpp"
#include "object_reader.hpp"

//!
Curve::Curve( const string& ObjectName,
              const string& ObjectKind ) : MarketData( ObjectName, ObjectKind )
{
}

//! _value_list is owned by its LaVector wrapper; nothing to free by hand
Curve::~Curve() = default;

//! read the term-structure pillars (dates + values)
void Curve::Configure( ObjectReader& reader )
{
    SetDateList( reader.Get<vector<date>>( "dates" ) );
    SetValueList( reader.LaVector( "values" ) );
}

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

//! Term-structured rate leg for the MCL drift: a YieldCurveNode that samples this
//! curve's zero rate at every diffusion date, so the simulated drift follows the
//! whole curve instead of a single front-pillar rate. For a flat curve every zero
//! rate equals the flat rate, so the behaviour is unchanged there.
MonteCarloNode* Curve::GetNode( NodeCollector& NC )
{

    return NC.GetOrCreate<YieldCurveNode>( _name,
                                           [&]( YieldCurveNode* Y )
                                           { Y->SetCurve( this ); } );
}