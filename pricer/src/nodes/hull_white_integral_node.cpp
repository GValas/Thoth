#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

HullWhiteIntegralNode::HullWhiteIntegralNode( const string& Name ) : MonteCarloNode( Name )
{
}

HullWhiteIntegralNode::~HullWhiteIntegralNode() = default;

//! V(t) = Var( int_0^t x du ) — same closed form as HullWhite::VarIntegral,
//! duplicated on the node so the graph carries plain parameters, not object links
double HullWhiteIntegralNode::VarIntegral( double t ) const
{
    const double b = ( 1 - exp( -_a * t ) ) / _a;
    const double c = ( 1 - exp( -2 * _a * t ) ) / ( 2 * _a );
    return _sigma * _sigma / ( _a * _a ) * ( t - 2 * b + c );
}

//! trapezoid step of int x plus the V/2 convexity increment. The trapezoid is the
//! only discretisation bias of the hybrid (the OU factor is exact); it is second
//! order in the step and vanishes on the MC grid refinements.
void HullWhiteIntegralNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex == 0 )
    {
        _value_list[DateIndex] = 0;
        return;
    }
    const double dt = _dt_list[DateIndex];
    const double x1 = _factor_node->GetValue( DateIndex - 1 );
    const double x2 = _factor_node->GetValue( DateIndex );
    const double dv = ( VarIntegral( _t_list[DateIndex] ) - VarIntegral( _t_list[DateIndex - 1] ) ) / 2;
    _value_list[DateIndex] = _value_list[DateIndex - 1] + dt * ( x1 + x2 ) / 2 + dv;
}

//! X(0) = 0 is path-independent, so the scheduler treats date 0 as a constant
bool HullWhiteIntegralNode::IsConstant( size_t DateIndex )
{
    return ( DateIndex == 0 );
}

void HullWhiteIntegralNode::SetParameters( double A, double Sigma )
{
    _a = A;
    _sigma = Sigma;
}

void HullWhiteIntegralNode::SetFactorNode( MonteCarloNode* N )
{
    _factor_node = N;
}

//! recursion: the factor at both trapezoid ends and this node's previous value
void HullWhiteIntegralNode::GetDateDependencies( size_t DateIndex,
                                                 vector<MonteCarloNode*>& NodeList,
                                                 vector<size_t>& DateList )
{
    if ( DateIndex > 0 )
    {
        NodeList.push_back( _factor_node );
        DateList.push_back( DateIndex );
        NodeList.push_back( _factor_node );
        DateList.push_back( DateIndex - 1 );
        NodeList.push_back( this );
        DateList.push_back( DateIndex - 1 );
    }
}
