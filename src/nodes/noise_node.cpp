#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

NoiseNode::NoiseNode( const string& Name ) : MonteCarloNode( Name )
{
    _random_generator = nullptr;
}

NoiseNode::~NoiseNode() = default;

void NoiseNode::ComputeValue( size_t DateIndex )
{
    //! Sobol + Brownian-bridge increments when wired, else an independent draw
    _value_list[DateIndex] = _quasi_noise ? ( *_quasi_noise )[DateIndex]
                                          : _random_generator->Gaussian();
}

void NoiseNode::SetRandomGenerator( Rng* RandomGenerator )
{
    _random_generator = RandomGenerator;
}

void NoiseNode::SetNoiseBuffer( const vector<double>* Buffer )
{
    _quasi_noise = Buffer;
}

void NoiseNode::GetDateDependencies( size_t /*DateIndex*/,
                                     vector<MonteCarloNode*>& /*NodeList*/,
                                     vector<size_t>& /*DateList*/ )
{
}
