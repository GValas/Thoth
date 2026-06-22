#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

//! Construct a named noise leaf with no source attached yet; the engine wires the
//! RNG (and optionally a quasi-noise buffer) before the first path.
NoiseNode::NoiseNode( const string& Name ) : MonteCarloNode( Name )
{
    _random_generator = nullptr;
}

NoiseNode::~NoiseNode() = default;

//! Produce this date's standard-normal innovation. Side effect: writes
//! _value_list[DateIndex]. The quasi-noise buffer (Sobol + Brownian bridge) wins
//! when wired; otherwise consume one Gaussian from the shared pseudo-random stream.
//! The draw order across nodes is fixed by the topological sort, which keeps the
//! pseudo-random path reproducible.
void NoiseNode::ComputeValue( size_t DateIndex )
{
    //! Sobol + Brownian-bridge increments when wired, else an independent draw
    _value_list[DateIndex] = _quasi_noise ? ( *_quasi_noise )[DateIndex]
                                          : _random_generator->Gaussian();
}

//! Set the pseudo-random Gaussian source (non-owning).
void NoiseNode::SetRandomGenerator( Rng* RandomGenerator )
{
    _random_generator = RandomGenerator;
}

//! Attach (or, with nullptr, detach) the PathGenerator factor buffer that supplies
//! quasi-random normalized increments instead of independent draws.
void NoiseNode::SetNoiseBuffer( const vector<double>* Buffer )
{
    _quasi_noise = Buffer;
}

//! Leaf source node: it has no DAG children, so it declares no dependencies.
void NoiseNode::GetDateDependencies( size_t /*DateIndex*/,
                                     vector<MonteCarloNode*>& /*NodeList*/,
                                     vector<size_t>& /*DateList*/ )
{
}
