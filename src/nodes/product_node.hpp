#pragma once
#include "monte_carlo_node.hpp"

class ProductNode : public MonteCarloNode
{
  private:
    vector<MonteCarloNode*> _node_list;
    vector<double> _pow_list;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    vector<MonteCarloNode*> GetNodeList();

    //! setter
    void PushNode( MonteCarloNode* N,
                   double Pow );

    ProductNode( const string& Name );
    ~ProductNode() override;
};
