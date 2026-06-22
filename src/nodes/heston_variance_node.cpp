#include "thoth.hpp"
#include "nodes.hpp"

HestonVarianceNode::HestonVarianceNode( const string& Name ) : MonteCarloNode( Name ) {}
HestonVarianceNode::~HestonVarianceNode() = default;

//! initial variance and the CIR speed/level/vol-of-vol
void HestonVarianceNode::SetParameters( double V0, double Kappa, double Theta, double Xi )
{
    _v0 = V0;
    _kappa = Kappa;
    _theta = Theta;
    _xi = Xi;
}

//! Andersen (2008) QE step for the CIR variance.
void HestonVarianceNode::ComputeValue( size_t DateIndex )
{
    if ( DateIndex == 0 )
    {
        _value_list[DateIndex] = _v0; //!< seed the path at the initial variance
        return;
    }

    const double v = _value_list[DateIndex - 1]; //!< previous variance v_t
    const double dt = _dt_list[DateIndex];
    const double z = _noise_node->GetValue( DateIndex ); //!< N(0,1)

    //! exact first two conditional moments of v_{t+dt} | v_t for the CIR process:
    //! mean m and variance s2 (closed form, no Euler bias). psi = s2/m^2 is the
    //! switching statistic that selects the QE branch (Andersen's threshold 1.5).
    const double e = exp( -_kappa * dt );
    const double m = _theta + ( v - _theta ) * e;
    const double s2 = v * _xi * _xi * e / _kappa * ( 1 - e ) +
                      _theta * _xi * _xi / ( 2 * _kappa ) * ( 1 - e ) * ( 1 - e );
    const double psi = ( m > 0 ) ? s2 / ( m * m ) : 0;

    if ( psi <= 1.5 ) //!< quadratic branch : moderate-to-high variance, v = a(b+z)^2
    {
        //! match the moments of a non-central-chi-square-like a*(b+Z)^2 by solving for
        //! b^2 and a from m and psi (Andersen eq. 27-28)
        const double c = 2.0 / psi;
        const double b2 = c - 1.0 + sqrt( c ) * sqrt( c - 1.0 );
        const double a = m / ( 1.0 + b2 );
        const double bz = sqrt( b2 ) + z;
        _value_list[DateIndex] = a * bz * bz;
    }
    else //!< exponential branch : low variance, a spike-at-0 + exponential tail sampled
         //!< by inverting the CDF with a uniform u
    {
        const double p = ( psi - 1.0 ) / ( psi + 1.0 );  //!< point mass at 0
        const double beta = ( 1.0 - p ) / m;             //!< rate of the exponential tail
        const double u = 0.5 * erfc( -z / sqrt( 2.0 ) ); //!< Phi(z) ~ U(0,1): reuse z as the uniform
        //! inverse-CDF: below the mass p map to exactly 0, else the exponential quantile
        _value_list[DateIndex] = ( u <= p ) ? 0.0 : log( ( 1.0 - p ) / ( 1.0 - u ) ) / beta;
    }
}

//! the QE step reads v at the previous date (self-edge) and the current noise
void HestonVarianceNode::GetDateDependencies( size_t DateIndex,
                                              vector<MonteCarloNode*>& NodeList,
                                              vector<size_t>& DateList )
{
    if ( DateIndex > 0 )
    {
        NodeList.push_back( this );
        DateList.push_back( DateIndex - 1 );
        NodeList.push_back( _noise_node );
        DateList.push_back( DateIndex );
    }
}
