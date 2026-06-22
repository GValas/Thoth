#pragma once
#include "monte_carlo_node.hpp"

//! Absolute basket level: the weighted sum sum_i w_i * S_i of its component spot
//! nodes (no rebasing), used as the underlying of a basket option.
class AbsoluteBasketNode : public MonteCarloNode
{

  private:
    vector<MonteCarloNode*> _underlying_node_list; //!< component spot nodes S_i
    vector<double> _weight_list;                   //!< basket weights w_i, parallel to _underlying_node_list

  public:
    //! basket level for the date: sum_i w_i * S_i(DateIndex), read from the children
    void ComputeValue( size_t DateIndex ) override;
    //! every component spot at the same date is a child (the weighted sum is read at DateIndex)
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    vector<MonteCarloNode*> GetUnderlyingNodeList(); //!< the component spot nodes (graph wiring / inspection)

    //! setter
    void PushUnderlying( MonteCarloNode* N ); //!< append one component spot node (paired with PushWeight)
    void PushWeight( double Weight );         //!< append the weight for the matching component

    AbsoluteBasketNode( const string& Name );
    ~AbsoluteBasketNode() override;
};
