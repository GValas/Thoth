#pragma once
#include "monte_carlo_node.hpp"

class AbsoluteBasketNode : public MonteCarloNode
{

  private:
    vector<MonteCarloNode*> _underlying_node_list;
    vector<double> _weight_list;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    vector<MonteCarloNode*> GetUnderlyingNodeList();

    //! setter
    void PushUnderlying( MonteCarloNode* N );
    void PushWeight( double Weight );

    AbsoluteBasketNode( const string& Name );
    ~AbsoluteBasketNode() override;
};
