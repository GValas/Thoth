#pragma once
#include "monte_carlo_node.hpp"

//! Book level: the sum of its contract nodes' premiums, each converted to the book
//! currency by an optional forex node (factor 1 for a same-currency book). An
//! indicator node — the book premium/trust is reported.
class BookNode : public MonteCarloNode
{

  private:
    vector<MonteCarloNode*> _contract_node_list; //!< per-contract premium nodes
    vector<MonteCarloNode*> _forex_node_list;    //!< parallel FX-to-book-ccy nodes (entry may be null)

  public:
    //! book premium = sum of contract premiums each multiplied by its FX factor
    void ComputeValue( size_t DateIndex ) override;
    //! each contract (and its FX node when present) at the same date is a child
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void PushContractNode( MonteCarloNode* N ); //!< append a contract premium node
    void PushForexNode( MonteCarloNode* N );    //!< append the matching FX node (null = same currency)

    //! getter

    BookNode( const string& Name );
    ~BookNode() override;
};
