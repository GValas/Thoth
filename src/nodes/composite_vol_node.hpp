#pragma once
#include "monte_carlo_node.hpp"

//! Composite (quanto) volatility of S*FX: sqrt(v_S^2 + v_X^2 + 2 rho(S,X) v_S v_X),
//! combined from the underlying vol, the FX vol and their correlation.
class CompositeVolNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _rho_SX_node = nullptr; //!< rho(S, X) between underlying and FX
    MonteCarloNode* _vol_S_node = nullptr;  //!< vol of the underlying S
    MonteCarloNode* _vol_X_node = nullptr;  //!< vol of the FX rate X

  public:
    //! composite vol sqrt(v_S^2 + v_X^2 + 2 rho v_S v_X) of the product S*X
    void ComputeValue( size_t DateIndex ) override;
    //! reads the correlation and both vols at the same date
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter

    //! setter
    void SetRhoSXNode( MonteCarloNode* N ); //!< wire rho(S, X)
    void SetVolSNode( MonteCarloNode* N );  //!< wire vol(S)
    void SetVolXNode( MonteCarloNode* N );  //!< wire vol(X)

    CompositeVolNode( const string& Name );
    ~CompositeVolNode() override;
};
