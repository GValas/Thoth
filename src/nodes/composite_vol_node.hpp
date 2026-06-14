#pragma once
#include "monte_carlo_node.hpp"

class CompositeVolNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _rho_SX_node;
    MonteCarloNode* _vol_S_node;
    MonteCarloNode* _vol_X_node;

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
