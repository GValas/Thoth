#pragma once
#include "monte_carlo_node.hpp"

//! Risk-neutral carry rate r_dom - r_for - repo - dividend (each leg optional) —
//! the log-drift the spot diffusion grows at.
class DriftNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _domestic_rate_node; //!< r_dom (the funding/discount leg); null => 0
    MonteCarloNode* _foreign_rate_node;  //!< r_for (FX / quanto foreign leg); null => 0
    MonteCarloNode* _repo_node;          //!< repo / borrow cost leg; null => 0
    MonteCarloNode* _dividend_node;      //!< continuous dividend-yield leg; null => 0

  public:
    //! net carry r_dom - r_for - repo - dividend, each leg dropped when its node is null
    void ComputeValue( size_t DateIndex ) override;
    //! only the non-null legs are declared as children
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    MonteCarloNode* GetDomesticRateNode(); //!< r_dom node (graph wiring / inspection)
    MonteCarloNode* GetForeignRateNode();  //!< r_for node
    MonteCarloNode* GetRepoNode();         //!< repo node
    MonteCarloNode* GetDividendNode();     //!< dividend-yield node

    //! setter
    void SetDomesticRateNode( MonteCarloNode* N ); //!< wire r_dom (+)
    void SetForeignRateNode( MonteCarloNode* N );  //!< wire r_for (-)
    void SetRepoRateNode( MonteCarloNode* N );     //!< wire repo (-)
    void SetDividendRateNode( MonteCarloNode* N ); //!< wire dividend yield (-)

    DriftNode( const string& Name );
    ~DriftNode() override;
};
