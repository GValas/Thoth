#pragma once
#include "monte_carlo_node.hpp"

class DriftNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _domestic_rate_node;
    MonteCarloNode* _foreign_rate_node;
    MonteCarloNode* _repo_node;
    MonteCarloNode* _dividend_node;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    MonteCarloNode* GetDomesticRateNode();
    MonteCarloNode* GetForeignRateNode();
    MonteCarloNode* GetRepoNode();
    MonteCarloNode* GetDividendNode();

    //! setter
    void SetDomesticRateNode( MonteCarloNode* N );
    void SetForeignRateNode( MonteCarloNode* N );
    void SetRepoRateNode( MonteCarloNode* N );
    void SetDividendRateNode( MonteCarloNode* N );

    DriftNode( const string& Name );
    ~DriftNode() override;
};
