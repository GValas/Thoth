#pragma once
#include "monte_carlo_node.hpp"

//! One priced contract: discounts each payoff (flow) node from its flow date back
//! to the evaluation date with the contract's rate curve and sums them. An
//! indicator node — the per-contract premium/trust is reported.
class ContractNode : public MonteCarloNode
{
  private:
    vector<size_t> _flow_date_index_list;       //!< flow date index per flow node (parallel to _flow_node_list)
    vector<MonteCarloNode*> _flow_node_list;    //!< payoff/flow nodes paying on those dates
    MonteCarloNode* _rate_curve_node = nullptr; //!< zero-rate curve used to discount each flow
    //! optional Hull-White exponent X(t) = int x + V/2: when set, each discount
    //! factor is multiplied by exp( X(t1) - X(t2) ) so the PATHWISE stochastic
    //! discount exp(-int r) applies (the deterministic z t part stays on the
    //! zero-rate curve node above, keeping the rho bump path unchanged)
    MonteCarloNode* _hw_exponent_node = nullptr;

  public:
    //! contract premium = sum of each flow discounted from its flow date to DateIndex
    void ComputeValue( size_t DateIndex ) override;
    //! each flow (read at its own flow date) and the rate curve at that date are children
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter

    //! setters
    //! append a flow node together with the date index on which it pays
    void PushFlowNode( MonteCarloNode* N,
                       size_t FlowDateIndex );
    void SetRateCurveNode( MonteCarloNode* N );  //!< wire the discounting zero-rate curve
    void SetHwExponentNode( MonteCarloNode* N ); //!< wire the stochastic-rate exponent (Hull-White)

    ContractNode( const string& Name );
    ~ContractNode() override;
};
