#pragma once
#include "monte_carlo_node.hpp"

class SpotDiffusionNode : public MonteCarloNode
{
  private:
    double _spot;
    MonteCarloNode* _local_vol_node;
    MonteCarloNode* _drift_node;
    MonteCarloNode* _brownian_node;

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

    SpotDiffusionNode( const string& Name );
    ~SpotDiffusionNode() override;
};
