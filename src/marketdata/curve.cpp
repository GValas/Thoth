#include "thoth.hpp"
#include "curve.hpp"

//!
Curve::Curve( const string& ObjectName,
              const string& ObjectKind ) : MarketData( ObjectName, ObjectKind )
{
}

//! _value_list is owned by its GslVector wrapper; nothing to free by hand
Curve::~Curve() = default;

//! setter
void Curve::SetDateList( const vector<date>& DateList )
{
    CheckDateList( DateList );
    _date_list = DateList;
}

//! setter
void Curve::SetValueList( gsl_vector* ValueList )
{
    _value_list = ValueList;
    gsl_vector_scale( _value_list, 0.01 );
}

//! flat curve: returns the first point regardless of maturity (term-structure
//! interpolation is not implemented — see TODO.md Phase 6 / "collapse marketdata")
double Curve::GetCurveValue( const date& /*MaturityDate*/ )
{
    return gsl_vector_get( _value_list, 0 ) + _curve_shift;
}

MonteCarloNode* Curve::GetNode( NodeCollector& NC )
{

    return NC.GetOrCreate<ConstantNode>( _name,
                                         [&]( ConstantNode* Y )
                                         { Y->SetConstantValue( GetCurveValue( date( 0 ) ) ); } );
}