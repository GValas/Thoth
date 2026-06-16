#pragma once
#include "monte_carlo_node.hpp"

//! Quanto adjustment: multiplies the diffused spot by exp(-rho(S,FX) sigma_S
//! sigma_FX t), the change-of-measure drift correction applied to an asset whose
//! payoff is settled in a foreign currency.
class QuantoAdjustmentNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _udl_vol_node;
    MonteCarloNode* _fx_vol_node;
    MonteCarloNode* _udl_fx_correl_node;
    MonteCarloNode* _udl_spot_node;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    MonteCarloNode* GetUdlVolNode();
    MonteCarloNode* GetFxVolNode();
    MonteCarloNode* GetUdlFxCorrelNode();
    MonteCarloNode* GetUdlSpotNode();

    //! setter
    void SetUdlVolNode( MonteCarloNode* N );
    void SetFxVolNode( MonteCarloNode* N );
    void SetUdlFxCorrelNode( MonteCarloNode* N );
    void SetUdlSpotNode( MonteCarloNode* N );

    QuantoAdjustmentNode( const string& Name );
    ~QuantoAdjustmentNode() override;
};
