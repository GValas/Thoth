#pragma once
#include "monte_carlo_node.hpp"

class ContractNode : public MonteCarloNode
{
  private:
    vector<size_t> _flow_date_index_list;
    vector<MonteCarloNode*> _flow_node_list;
    MonteCarloNode* _rate_curve_node;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    vector<size_t> GetFlowDateIndexList();
    vector<MonteCarloNode*> GetFlowNodeList();
    MonteCarloNode* GetRateCurveNode();

    //! setters
    void PushFlowNode( MonteCarloNode* N,
                       size_t FlowDateIndex );
    void SetRateCurveNode( MonteCarloNode* N );

    ContractNode( const string& Name );
    ~ContractNode() override;
};
