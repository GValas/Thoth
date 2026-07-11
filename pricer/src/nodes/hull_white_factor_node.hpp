#pragma once
#include "monte_carlo_node.hpp"

//! The Hull-White OU factor x(t): dx = -a x dt + sigma dW, x(0) = 0, diffused with
//! the EXACT transition (no discretisation bias in the factor itself)
//!   x_i = x_{i-1} e^{-a dt_i} + sigma sqrt( (1 - e^{-2 a dt_i}) / (2a) ) eps_i,
//! eps_i the (Cholesky-correlated) N(0,1) increment of the "<ccy>_ir" factor.
//! The short rate is r(t) = x(t) + alpha(t); alpha never appears here — its
//! integral is folded into the deterministic legs (see HullWhiteIntegralNode).
class HullWhiteFactorNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _noise_node = nullptr; //!< correlated N(0,1) source ("<ccy>_ir#noise")
    double _a = 0;                         //!< mean reversion
    double _sigma = 0;                     //!< short-rate vol (absolute)

  public:
    //! exact OU recursion; x(0) = 0
    void ComputeValue( size_t DateIndex ) override;
    //! x(0) = 0 is path-independent
    bool IsConstant( size_t DateIndex ) override;
    //! children: the noise at this date and this node's own previous value
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setters
    void SetParameters( double A, double Sigma );
    void SetNoiseNode( MonteCarloNode* N );

    HullWhiteFactorNode( const string& Name );
    ~HullWhiteFactorNode() override;
};
