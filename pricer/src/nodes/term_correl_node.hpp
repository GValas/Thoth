#pragma once
#include "monte_carlo_node.hpp"

//! Deterministic per-date correlation values for a term-structured correlation
//! matrix: the correlation object supplies an evaluator (idx, t) -> value that the
//! node samples at every diffusion date. Two flavours share the class:
//!  - quanto / composite correlations carry the RUNNING AVERAGE rho_bar(t) =
//!    (1/t) * integral_0^t rho(u) du, because their consumer applies the factor
//!    exp(-sigma_S sigma_FX rho * t) over the whole horizon [0, t];
//!  - Cholesky coefficients carry the per-STEP factor L_i of the step-averaged
//!    matrix over [t_{i-1}, t_i], so the Brownian increments reproduce the exact
//!    integrated covariance (an optional prepare hook lets the correlation build
//!    the whole factor schedule once, before the first entry is read).
//! Values are path-independent: like YieldCurveNode the whole vector is filled on
//! the first ComputeValue and every later call is a no-op. For a constant matrix
//! the factories keep returning ConstantNode, so this node never appears.
class TermCorrelNode : public MonteCarloNode
{

  private:
    //! (date index, year-fraction from t0) -> value, supplied by the Correlation
    std::function<double( size_t, double )> _evaluate;
    //! optional one-shot hook handed the full schedule before the fill (used by the
    //! Cholesky flavour to factorise every step-average matrix in one pass)
    std::function<void( const vector<double>& )> _prepare;
    bool _filled = false; //!< true once the whole value vector has been populated

  public:
    //! fill the entire vector on first call, then no-op (path-independent).
    void ComputeValue( size_t DateIndex ) override;
    //! leaf node (reads the correlation object, not other nodes): no dependencies.
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setters: the per-date evaluator, and the optional schedule-wide prepare hook
    void SetEvaluator( std::function<double( size_t, double )> Evaluate );
    void SetPrepare( std::function<void( const vector<double>& )> Prepare );

    TermCorrelNode( const string& Name );
    ~TermCorrelNode() override;
};
