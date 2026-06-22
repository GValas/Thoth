#pragma once
#include "monte_carlo_node.hpp"

//! Heston spot diffusion using Andersen's (2008) log-spot scheme: the spot/
//! variance correlation rho is folded into the K0..K2 drift coefficients (via
//! the variance increment), and the residual is an independent Gaussian. Reads
//! the variance node at this and the previous date, the carry (drift) node, and
//! one independent N(0,1) noise.
class HestonSpotNode : public MonteCarloNode
{
  private:
    double _spot = 0;                         //!< S(0), the initial spot (value at date 0)
    double _kappa = 0;                        //!< CIR mean-reversion speed
    double _theta = 0;                        //!< CIR long-run variance
    double _xi = 0;                           //!< vol-of-vol (xi=0 collapses to the log-Euler branch)
    double _rho = 0;                          //!< spot/variance correlation, folded into K0..K2
    MonteCarloNode* _variance_node = nullptr; //!< CIR variance v_t (read at this and previous date)
    MonteCarloNode* _drift_node = nullptr;    //!< cumulative carry (r - q - repo) to each date
    MonteCarloNode* _noise_node = nullptr;    //!< independent N(0,1) for the residual (orthogonal to rho)
    MonteCarloNode* _jump_node = nullptr;     //!< optional Bates jump increment (null = pure Heston)

  public:
    //! true only at date 0 (the fixed initial spot)
    bool IsConstant( size_t DateIndex ) override;
    //! one Andersen log-spot step S_i = S_{i-1} * exp(step) along the path
    void ComputeValue( size_t DateIndex ) override;
    //! depends on variance (this & previous date), drift (this & previous), noise, the
    //! optional jump, and this node's own previous spot
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    void SetParameters( double Kappa, double Theta, double Xi, double Rho ); //!< CIR + correlation params
    void SetVarianceNode( MonteCarloNode* N ) { _variance_node = N; }        //!< wire the CIR variance node
    void SetDriftNode( MonteCarloNode* N ) { _drift_node = N; }              //!< wire the cumulative-carry node
    void SetNoiseNode( MonteCarloNode* N ) { _noise_node = N; }              //!< wire the residual N(0,1) noise
    void SetJumpNode( MonteCarloNode* N ) { _jump_node = N; }                //!< wire the Bates jump node (optional)
    void SetSpot( double Spot );                                             //!< set S(0) and seed _value_list[0]

    HestonSpotNode( const string& Name );
    ~HestonSpotNode() override;
};
