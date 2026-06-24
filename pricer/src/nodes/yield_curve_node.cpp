#include "thoth.hpp"
#include "nodes.hpp"
#include "curve.hpp"

//************************************************************************/

//! Construct with no curve attached; SetCurve wires it before evaluation.
YieldCurveNode::YieldCurveNode( const string& Name ) : MonteCarloNode( Name )
{
}

YieldCurveNode::~YieldCurveNode() = default;

//! Populate the per-date zero rates. Side effect: fills _value_list and sets
//! _filled. The DateIndex argument is ignored: the values are path-independent, so
//! the whole vector is computed on the first call and every later call returns
//! immediately (the engine still invokes it once per date per path).
void YieldCurveNode::ComputeValue( size_t /*DateIndex*/ )
{
    //! zero-rate term structure: value at each date is the curve's continuously-
    //! compounded zero rate to that date (the rho shift is inside GetCurveValue).
    //! Path-independent, so fill the whole vector once and skip on every later call.
    if ( _filled )
    {
        return;
    }
    //! one zero rate per diffusion date; null curve degrades to a 0 rate.
    for ( size_t i = 0; i < _date_list.size(); i++ )
    {
        _value_list[i] = _curve ? _curve->GetCurveValue( _date_list[i] ) : 0.0;
    }
    _filled = true;
}

//! attach the zero curve to sample (non-owning).
void YieldCurveNode::SetCurve( Curve* C )
{
    _curve = C;
}

//! Leaf node: its values come from the curve, not from other graph nodes, so it
//! declares no dependencies for the topological sort.
void YieldCurveNode::GetDateDependencies( size_t /*DateIndex*/,
                                          vector<MonteCarloNode*>& /*NodeList*/,
                                          vector<size_t>& /*DateList*/ )
{
}
