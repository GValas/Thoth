#pragma once
#include "monte_carlo_node.hpp"

//! The cumulative Hull-White exponent X(t_i) = int_0^{t_i} x du + V(t_i)/2:
//! the pathwise part is the trapezoid accumulation of the OU factor (bias
//! O(dt^2), the factor itself being exact), and V(t)/2 is the deterministic
//! convexity that fits the initial curve — int_0^T alpha = z(T) T + V(T)/2, the
//! z T part staying on the (bumpable) yield-curve nodes. Consumers:
//!   - the ContractNode multiplies its deterministic discount by exp(-X)
//!     (so E[exp(-int r)] = P(0,T) by construction);
//!   - the HybridSpotNode multiplies the deterministic-carry spot by exp(+X)
//!     (the equity drifts at the stochastic rate; the forward is unchanged).
class HullWhiteIntegralNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _factor_node = nullptr; //!< the OU factor x(t)
    double _a = 0;                          //!< mean reversion (for V(t))
    double _sigma = 0;                      //!< short-rate vol (for V(t))

    //! V(t) = (sigma/a)^2 ( t - 2(1-e^{-at})/a + (1-e^{-2at})/(2a) )
    double VarIntegral( double t ) const;

  public:
    //! X_i = X_{i-1} + dt (x_{i-1}+x_i)/2 + ( V(t_i) - V(t_{i-1}) )/2; X_0 = 0
    void ComputeValue( size_t DateIndex ) override;
    //! X(0) = 0 is path-independent
    bool IsConstant( size_t DateIndex ) override;
    //! children: the factor at this and the previous date, and self at previous
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setters
    void SetParameters( double A, double Sigma );
    void SetFactorNode( MonteCarloNode* N );

    HullWhiteIntegralNode( const string& Name );
    ~HullWhiteIntegralNode() override;
};
