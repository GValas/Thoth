#include "thoth.hpp"
#include "nodes.hpp"

HestonSpotNode::HestonSpotNode( const string& Name ) : MonteCarloNode( Name ) {}
HestonSpotNode::~HestonSpotNode() = default;

void HestonSpotNode::SetParameters( double Kappa, double Theta, double Xi, double Rho )
{
    _kappa = Kappa;
    _theta = Theta;
    _xi = Xi;
    _rho = Rho;
}

void HestonSpotNode::SetSpot( double Spot )
{
    _spot = Spot;
    _value_list[0] = _spot;
}

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
    const double b = _drift_node->GetValue( DateIndex ); //!< carry r - q (- repo)
    const double z = _noise_node->GetValue( DateIndex );  //!< independent N(0,1)

    double step;
    if ( _xi > 1e-12 )
    {
        const double g = 0.5;
        const double K0 = -_rho * _kappa * _theta / _xi * dt;
        const double K1 = g * dt * ( _kappa * _rho / _xi - 0.5 ) - _rho / _xi;
        const double K2 = g * dt * ( _kappa * _rho / _xi - 0.5 ) + _rho / _xi;
        const double K3 = g * dt * ( 1.0 - _rho * _rho );
        const double K4 = K3;
        double var = K3 * vp + K4 * vc;
        if ( var < 0 )
        {
            var = 0;
        }
        step = b * dt + K0 + K1 * vp + K2 * vc + sqrt( var ) * z;
    }
    else //!< degenerate (no vol-of-vol): plain log-Euler on the (still CIR) variance
    {
        step = ( b - 0.5 * vp ) * dt + sqrt( vp * dt ) * z;
    }

    _value_list[DateIndex] = _value_list[DateIndex - 1] * exp( step );
}

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
        NodeList.push_back( _noise_node );
        DateList.push_back( DateIndex );
        NodeList.push_back( this );
        DateList.push_back( DateIndex - 1 );
    }
}
