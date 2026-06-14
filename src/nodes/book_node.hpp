#pragma once
#include "monte_carlo_node.hpp"

class BookNode : public MonteCarloNode
{

  private:
    vector<MonteCarloNode*> _contract_node_list;
    vector<MonteCarloNode*> _forex_node_list;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void PushContractNode( MonteCarloNode* N );
    void PushForexNode( MonteCarloNode* N );

    //! getter
    vector<MonteCarloNode*> GetContractNodeList();
    vector<MonteCarloNode*> GetForexNodeList();

    BookNode( const string& Name );
    ~BookNode() override;
};
