#include "thoth.hpp"
#include "nodes.hpp"
#include "curve.hpp"

//************************************************************************/

YieldCurveNode::YieldCurveNode( const string& Name ) : MonteCarloNode( Name )
{
}

YieldCurveNode::~YieldCurveNode() = default;

void YieldCurveNode::ComputeValue( size_t /*DateIndex*/ )
{
    //! zero-rate term structure: value at each date is the curve's continuously-
    //! compounded zero rate to that date (the rho shift is inside GetCurveValue).
    //! Path-independent, so fill the whole vector once and skip on every later call.
    if ( _filled )
    {
        return;
    }
    for ( size_t i = 0; i < _date_list.size(); i++ )
    {
        _value_list[i] = _curve ? _curve->GetCurveValue( _date_list[i] ) : 0.0;
    }
    _filled = true;
}

void YieldCurveNode::SetCurve( Curve* C )
{
    _curve = C;
}

void YieldCurveNode::GetDateDependencies( size_t /*DateIndex*/,
                                          vector<MonteCarloNode*>& /*NodeList*/,
                                          vector<size_t>& /*DateList*/ )
{
}
