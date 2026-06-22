#pragma once
#include "monte_carlo_node.hpp"

//! Correlation between a composite underlying S*FX and another factor, assembled
//! from the underlying/FX sub-correlations and the underlying, FX and composite
//! vols. Feeds the Cholesky combine that correlates a multi-asset composite book.
class CompositeCorrelNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _rho_S_AB_node = nullptr;  //!< rho(S, AB): underlying vs other factor
    MonteCarloNode* _rho_IJ_AB_node = nullptr; //!< rho(IJ, AB): FX leg vs other factor
    MonteCarloNode* _vol_S_node = nullptr;     //!< vol of the underlying S
    MonteCarloNode* _vol_IJ_node = nullptr;    //!< vol of the FX leg IJ
    MonteCarloNode* _vol_S_IJ_node = nullptr;  //!< composite vol of S.IJ (denominator)

  public:
    //! composite correlation rho(S.IJ, AB), assembled from the sub-correlations and vols
    void ComputeValue( size_t DateIndex ) override;
    //! all five inputs are read at the same date
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    MonteCarloNode* GetRhoSABNode();  //!< rho(S, AB) input node
    MonteCarloNode* GetRhoIJABNode(); //!< rho(IJ, AB) input node
    MonteCarloNode* GetVolSnode();    //!< vol(S) input node
    MonteCarloNode* GetVolIJNode();   //!< vol(IJ) input node
    MonteCarloNode* GetVolSIJNode();  //!< composite vol(S.IJ) input node

    //! setter
    void SetRhoSABNode( MonteCarloNode* N );  //!< wire rho(S, AB)
    void SetRhoIJABNode( MonteCarloNode* N ); //!< wire rho(IJ, AB)
    void SetVolSNode( MonteCarloNode* N );    //!< wire vol(S)
    void SetVolIJNode( MonteCarloNode* N );   //!< wire vol(IJ)
    void SetVolSIJNode( MonteCarloNode* N );  //!< wire composite vol(S.IJ)

    CompositeCorrelNode( const string& Name );
    ~CompositeCorrelNode() override;
};
