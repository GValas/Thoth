#include "thoth.hpp"
#include "nodes.hpp"

HestonSpotNode::HestonSpotNode( const string& Name ) : MonteCarloNode( Name ) {}
HestonSpotNode::~HestonSpotNode() = default;

//! CIR speed/level, vol-of-vol and the spot/variance correlation rho
void HestonSpotNode::SetParameters( double Kappa, double Theta, double Xi, double Rho )
{
    _kappa = Kappa;
    _theta = Theta;
    _xi = Xi;
    _rho = Rho;
}

//! store S(0) and seed date-0 directly (it never goes through a diffusion step)
void HestonSpotNode::SetSpot( double Spot )
{
    _spot = Spot;
    _value_list[0] = _spot;
}

//! S(0) is path-independent, so date 0 is treated as a constant by the scheduler
bool HestonSpotNode::IsConstant( size_t DateIndex )
{
    return ( DateIndex == 0 );
}

//! Andersen (2008) log-spot scheme (central discretisation, gamma1=gamma2=0.5):
//!   X_{t+dt} = X_t + b dt + K0 + K1 v_t + K2 v_{t+dt} + sqrt(K3 v_t + K4 v_{t+dt}) Z
//! with b the carry (drift node) and Z an independent N(0,1).
void HestonSpotNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex == 0 )
    {
        _value_list[DateIndex] = _spot;
        return;
    }

    const double dt = _dt_list[DateIndex];
    const double vp = _variance_node->GetValue( DateIndex - 1 );
    const double vc = _variance_node->GetValue( DateIndex );
    const double z = _noise_node->GetValue( DateIndex ); //!< independent N(0,1)

    //! term-structured carry over the step: the drift node holds the net zero rate
    //! (r - q - repo) to each date, so the deterministic log-drift is the difference
    //! of the cumulative carries (the integral of the forward carry across the step).
    //! For a flat carry c this is c*(t_i - t_{i-1}) = c*dt, the old flat behaviour.
    const double carry_drift = _drift_node->GetValue( DateIndex ) * _t_list[DateIndex] -
                               _drift_node->GetValue( DateIndex - 1 ) * _t_list[DateIndex - 1];

    double step;
    if ( _xi > 1e-12 )
    {
        //! Andersen K-coefficients with central weighting gamma1=gamma2=g=0.5. The
        //! correlated part of dW^S is reconstructed from the *variance increment*
        //! (rho/xi)*(v_{t+dt} - v_t - kappa(theta - v)dt) rather than from a separate
        //! correlated normal, which is what makes K0..K2 carry rho; K3/K4 weight the
        //! orthogonal residual variance (1-rho^2) split over the two endpoints.
        const double g = 0.5;
        const double K0 = -_rho * _kappa * _theta / _xi * dt;                  //!< constant drift offset
        const double K1 = g * dt * ( _kappa * _rho / _xi - 0.5 ) - _rho / _xi; //!< weight on v_t
        const double K2 = g * dt * ( _kappa * _rho / _xi - 0.5 ) + _rho / _xi; //!< weight on v_{t+dt}
        const double K3 = g * dt * ( 1.0 - _rho * _rho );                      //!< residual variance from v_t
        const double K4 = K3;                                                  //!< residual variance from v_{t+dt}
        double var = K3 * vp + K4 * vc;
        //! floor the residual variance: QE variance can dip slightly negative numerically
        if ( var < 0 )
        {
            var = 0;
        }
        step = carry_drift + K0 + K1 * vp + K2 * vc + sqrt( var ) * z;
    }
    else //!< degenerate (no vol-of-vol): plain log-Euler on the (still CIR) variance
    {
        //! -0.5 v dt is the Ito convexity term of d(log S); rho is irrelevant when xi=0
        step = carry_drift - 0.5 * vp * dt + sqrt( vp * dt ) * z;
    }

    //! Bates : add the compound-Poisson jump increment (compensator + realised
    //! jumps) for this step (no-op when there is no jump node)
    if ( _jump_node )
    {
        step += _jump_node->GetValue( DateIndex );
    }

    _value_list[DateIndex] = _value_list[DateIndex - 1] * exp( step );
}

//! wire the full Andersen step: variance at both step endpoints, the cumulative
//! carry at both endpoints (for the term-structured drift difference), the residual
//! noise, the optional jump, and the node's own previous spot. Date 0 has none.
void HestonSpotNode::GetDateDependencies( size_t DateIndex,
                                          vector<MonteCarloNode*>& NodeList,
                                          vector<size_t>& DateList )
{
    if ( DateIndex > 0 )
    {
        NodeList.push_back( _variance_node );
        DateList.push_back( DateIndex );
        NodeList.push_back( _variance_node );
        DateList.push_back( DateIndex - 1 );
        NodeList.push_back( _drift_node );
        DateList.push_back( DateIndex );
        //! the term-structured carry also reads the previous date's cumulative carry
        NodeList.push_back( _drift_node );
        DateList.push_back( DateIndex - 1 );
        NodeList.push_back( _noise_node );
        DateList.push_back( DateIndex );
        if ( _jump_node )
        {
            NodeList.push_back( _jump_node );
            DateList.push_back( DateIndex );
        }
        NodeList.push_back( this );
        DateList.push_back( DateIndex - 1 );
    }
}
