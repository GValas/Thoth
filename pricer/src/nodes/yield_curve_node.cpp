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
    //! one zero rate per diffusion date; null curve degrades to a 0 rate. GetCurveValue
    //! adds the curve's LIVE rho shift, but the bump-and-revalue sweep has already restored
    //! that to 0; swap it for the shift snapshotted at build time (_shift): subtract the
    //! live shift, add the frozen one. Base / non-rate nodes snapshot 0 -> unchanged.
    for ( size_t i = 0; i < _date_list.size(); i++ )
    {
        _value_list[i] = _curve
                             ? ( _curve->GetCurveValue( _date_list[i] ) - _curve->GetCurveShift() + _shift )
                             : 0.0;
    }
    _filled = true;
}

//! attach the zero curve to sample (non-owning) and freeze its current rho shift, so a
//! rate-bumped scenario node keeps discounting / drifting at the bumped rate even though
//! the bump is restored before the path sweep evaluates this node (see ComputeValue).
void YieldCurveNode::SetCurve( Curve* C )
{
    _curve = C;
    _shift = C ? C->GetCurveShift() : 0.0;
}

//! Leaf node: its values come from the curve, not from other graph nodes, so it
//! declares no dependencies for the topological sort.
void YieldCurveNode::GetDateDependencies( size_t /*DateIndex*/,
                                          vector<MonteCarloNode*>& /*NodeList*/,
                                          vector<size_t>& /*DateList*/ )
{
}
