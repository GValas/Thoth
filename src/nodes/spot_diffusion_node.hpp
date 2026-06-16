#pragma once
#include "monte_carlo_node.hpp"

class LocalVolatilityNode;

class SpotDiffusionNode : public MonteCarloNode
{
  private:
    double _spot;
    MonteCarloNode* _local_vol_node;
    MonteCarloNode* _drift_node;
    MonteCarloNode* _brownian_node;

    //! non-null -> add the log-space Milstein correction using this local-vol
    //! node's state-derivative (zero for a constant vol, so only set for local vol)
    LocalVolatilityNode* _milstein_lv = nullptr;

  public:
    bool IsConstant( size_t DateIndex ) override;
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getters
    MonteCarloNode* GetLocalVolNode();
    MonteCarloNode* GetBrownianNode();
    MonteCarloNode* GetDriftNode();

    //! setters
    void SetLocalVolNode( MonteCarloNode* N );
    void SetDriftNode( MonteCarloNode* N );
    void SetBrownianNode( MonteCarloNode* N );
    void SetSpot( double Spot );

    //! turn on the Milstein step, reading d(vol)/d(log spot) from the local-vol node
    void EnableMilstein( LocalVolatilityNode* Lv );

    SpotDiffusionNode( const string& Name );
    ~SpotDiffusionNode() override;
};
