#include "thoth.hpp"
#include "nodes.hpp"

#include <random>

JumpNode::JumpNode( const string& Name ) : MonteCarloNode( Name ) {}
JumpNode::~JumpNode() = default;

//! own a dedicated RNG so jump draws form an independent stream from the Brownian noise
void JumpNode::SetRandomGenerator( Rng* RandomGenerator )
{
    _rng = RandomGenerator;
}

//! cache the compensator kbar = E[e^J - 1] = exp(mu + sigma^2/2) - 1 alongside the
//! raw jump parameters so ComputeValue never recomputes it per step/path
void JumpNode::SetJumpParameters( double Lambda, double Mu, double Sigma )
{
    _lambda = Lambda;
    _mu = Mu;
    _sigma = Sigma;
    _kbar = exp( Mu + 0.5 * Sigma * Sigma ) - 1.0;
}

void JumpNode::ComputeValue( size_t DateIndex )
{
    //! no jump increment at t0, and a degenerate intensity disables jumps entirely
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
    //! of n lognormal jumps is N(n*mu, n*sigma^2). The per-step distributions are
    //! built once (dt is per-date) and reused, not reconstructed every path.
    if ( _poisson.empty() )
    {
        _poisson.reserve( _dt_list.size() );
        for ( double step_dt : _dt_list )
        {
            _poisson.emplace_back( _lambda * step_dt );
        }
    }
    unsigned int n = _poisson[DateIndex]( *_rng ); //!< number of jumps in this step
    if ( n > 0 )
    {
        //! sum of n iid N(mu, sigma^2) log jumps ~ N(n*mu, n*sigma^2): draw it in one
        //! shot as n*mu + sigma*sqrt(n)*Z rather than looping over individual jumps
        increment += n * _mu + _sigma * sqrt( (double)n ) * _rng->Gaussian();
    }

    _value_list[DateIndex] = increment;
}

//! self-contained RNG-driven node: no graph children (its own Poisson/Gaussian draws)
void JumpNode::GetDateDependencies( size_t /*DateIndex*/,
                                    vector<MonteCarloNode*>& /*NodeList*/,
                                    vector<size_t>& /*DateList*/ )
{
}
