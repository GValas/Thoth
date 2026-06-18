#pragma once
#include "monte_carlo_node.hpp"

class LocalVolatilityNode;

//! Spot diffusion: the log-Euler GBM step S_i = S_{i-1} exp((r - v^2/2) dt + v dW),
//! exact for a constant vol. For a local-vol surface v is read per step from a
//! LocalVolatilityNode and a log-space Milstein correction is added (EnableMilstein);
//! the correction vanishes for constant vol.
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

    //! optional discrete-dividend (escrow) node: future-dividend PV per date. When
    //! set, the node diffuses the clean escrowed process and publishes the observed
    //! spot (clean + future-dividend PV). Null -> no dividends (plain GBM).
    MonteCarloNode* _dividend_node = nullptr;

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
    void SetDividendNode( MonteCarloNode* N ); //!< optional discrete-dividend escrow
    void SetSpot( double Spot );

    //! turn on the Milstein step, reading d(vol)/d(log spot) from the local-vol node
    void EnableMilstein( LocalVolatilityNode* Lv );

    SpotDiffusionNode( const string& Name );
    ~SpotDiffusionNode() override;
};
