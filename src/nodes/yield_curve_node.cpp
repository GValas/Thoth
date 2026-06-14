#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

YieldCurveNode::YieldCurveNode( const string& Name ) : MonteCarloNode( Name )
{
}

YieldCurveNode::~YieldCurveNode() = default;

void YieldCurveNode::ComputeValue( size_t /*DateIndex*/ )
{
    //! never wired (discounting uses a ConstantNode); fail loudly if ever reached
    ERR( "YieldCurveNode '" + _name + "' : ComputeValue not implemented" );
}

void YieldCurveNode::GetDateDependencies( size_t /*DateIndex*/,
                                          vector<MonteCarloNode*>& /*NodeList*/,
                                          vector<size_t>& /*DateList*/ )
{
}
