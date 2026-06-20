#pragma once
#include "monte_carlo_node.hpp"

//! Correlation between a composite underlying S*FX and another factor, assembled
//! from the underlying/FX sub-correlations and the underlying, FX and composite
//! vols. Feeds the Cholesky combine that correlates a multi-asset composite book.
class CompositeCorrelNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _rho_S_AB_node = nullptr;
    MonteCarloNode* _rho_IJ_AB_node = nullptr;
    MonteCarloNode* _vol_S_node = nullptr;
    MonteCarloNode* _vol_IJ_node = nullptr;
    MonteCarloNode* _vol_S_IJ_node = nullptr;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    MonteCarloNode* GetRhoSABNode();
    MonteCarloNode* GetRhoIJABNode();
    MonteCarloNode* GetVolSnode();
    MonteCarloNode* GetVolIJNode();
    MonteCarloNode* GetVolSIJNode();

    //! setter
    void SetRhoSABNode( MonteCarloNode* N );
    void SetRhoIJABNode( MonteCarloNode* N );
    void SetVolSNode( MonteCarloNode* N );
    void SetVolIJNode( MonteCarloNode* N );
    void SetVolSIJNode( MonteCarloNode* N );

    CompositeCorrelNode( const string& Name );
    ~CompositeCorrelNode() override;
};
