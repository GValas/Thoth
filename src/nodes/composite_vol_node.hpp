#pragma once
#include "monte_carlo_node.hpp"

//! Composite (quanto) volatility of S*FX: sqrt(v_S^2 + v_X^2 + 2 rho(S,X) v_S v_X),
//! combined from the underlying vol, the FX vol and their correlation.
class CompositeVolNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _rho_SX_node = nullptr;
    MonteCarloNode* _vol_S_node = nullptr;
    MonteCarloNode* _vol_X_node = nullptr;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    MonteCarloNode* GetRhoSXNode();
    MonteCarloNode* GetVolSNode();
    MonteCarloNode* GetVolXNode();

    //! setter
    void SetRhoSXNode( MonteCarloNode* N );
    void SetVolSNode( MonteCarloNode* N );
    void SetVolXNode( MonteCarloNode* N );

    CompositeVolNode( const string& Name );
    ~CompositeVolNode() override;
};
