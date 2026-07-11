#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

HullWhiteFactorNode::HullWhiteFactorNode( const string& Name ) : MonteCarloNode( Name )
{
}

HullWhiteFactorNode::~HullWhiteFactorNode() = default;

//! exact OU step: the conditional law of x_i given x_{i-1} is Gaussian with mean
//! x_{i-1} e^{-a dt} and variance sigma^2 (1 - e^{-2 a dt}) / (2a) — no
//! discretisation bias whatever the step size.
void HullWhiteFactorNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex == 0 )
    {
        _value_list[DateIndex] = 0; //!< x(0) = 0 by the factor decomposition
        return;
    }
    const double dt = _dt_list[DateIndex];
    const double decay = exp( -_a * dt );
    const double stdev = _sigma * sqrt( ( 1 - decay * decay ) / ( 2 * _a ) );
    _value_list[DateIndex] =
        _value_list[DateIndex - 1] * decay + stdev * _noise_node->GetValue( DateIndex );
}

//! x(0) = 0 is path-independent, so the scheduler treats date 0 as a constant
bool HullWhiteFactorNode::IsConstant( size_t DateIndex )
{
    return ( DateIndex == 0 );
}

void HullWhiteFactorNode::SetParameters( double A, double Sigma )
{
    _a = A;
    _sigma = Sigma;
}

void HullWhiteFactorNode::SetNoiseNode( MonteCarloNode* N )
{
    _noise_node = N;
}

//! recursion: each step depends on the current noise and on this node's own
//! previous value (self-edge at DateIndex-1); date 0 has no dependencies
void HullWhiteFactorNode::GetDateDependencies( size_t DateIndex,
                                               vector<MonteCarloNode*>& NodeList,
                                               vector<size_t>& DateList )
{
    if ( DateIndex > 0 )
    {
        NodeList.push_back( _noise_node );
        DateList.push_back( DateIndex );
        NodeList.push_back( this );
        DateList.push_back( DateIndex - 1 );
    }
}
