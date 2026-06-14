#pragma once
#include "monte_carlo_node.hpp"

//! Monte-Carlo flow of a variance swap. At the maturity date index it reads the
//! whole simulated spot path (every diffusion date up to maturity), computes the
//! annualized realized variance RV = (1/T) * sum( ln(S_j / S_{j-1})^2 ), and
//! produces the cash flow notional * (RV - strike_variance). Zero on every other
//! date.
class VarianceSwapFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node = nullptr;
    double _strike_variance = 0; //!< K_var (decimal^2)
    double _notional = 1;
    size_t _flow_date_index = 0;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetSpotNode( MonteCarloNode* N );
    void SetStrikeVariance( double StrikeVariance );
    void SetNotional( double Notional );
    void SetFlowDateIndex( size_t DateIndex );

    VarianceSwapFlowNode( const string& Name );
    ~VarianceSwapFlowNode() override;
};
