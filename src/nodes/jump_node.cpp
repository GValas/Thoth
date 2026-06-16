#include "thoth.hpp"
#include "nodes.hpp"

#include <random>

JumpNode::JumpNode( const string& Name ) : MonteCarloNode( Name ) {}
JumpNode::~JumpNode() = default;

void JumpNode::SetRandomGenerator( Rng* RandomGenerator )
{
    _rng = RandomGenerator;
}

void JumpNode::SetJumpParameters( double Lambda, double Mu, double Sigma )
{
    _lambda = Lambda;
    _mu = Mu;
    _sigma = Sigma;
    _kbar = exp( Mu + 0.5 * Sigma * Sigma ) - 1.0;
}

void JumpNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex == 0 || _lambda <= 0 )
    {
        _value_list[DateIndex] = 0;
        return;
    }

    const double dt = _dt_list[DateIndex];
    //! martingale compensator: jumps add kbar*lambda*dt to the expected return,
    //! so the same amount is removed from the log-drift
    double increment = -_lambda * _kbar * dt;

    //! realised jumps over the step : n ~ Poisson(lambda*dt), aggregate log jump
    //! of n lognormal jumps is N(n*mu, n*sigma^2)
    std::poisson_distribution<unsigned int> poisson( _lambda * dt );
    unsigned int n = poisson( *_rng );
    if ( n > 0 )
    {
        increment += n * _mu + _sigma * sqrt( (double)n ) * _rng->Gaussian();
    }

    _value_list[DateIndex] = increment;
}

void JumpNode::GetDateDependencies( size_t /*DateIndex*/,
                                    vector<MonteCarloNode*>& /*NodeList*/,
                                    vector<size_t>& /*DateList*/ )
{
}
