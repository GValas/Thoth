#pragma once
#include "monte_carlo_node.hpp"

class LocalVolatilityNode;

//! Spot diffusion: the log-Euler GBM step S_i = S_{i-1} exp((r - v^2/2) dt + v dW),
//! exact for a constant vol. For a local-vol surface v is read per step from a
//! LocalVolatilityNode and a log-space Milstein correction is added (EnableMilstein);
//! the correction vanishes for constant vol.
//!
//! Role in the MC node graph: the stateful core of a single-asset path. Date 0 is
//! the spot today; each later date diffuses one Euler step from the previous date's
//! value, so the node depends on *itself* at DateIndex-1 (the only recursive
//! dependency in the graph). DAG inputs per step: the local/constant vol, the drift
//! (cumulative net carry, differenced into a per-step forward drift), the Brownian
//! increment, and optionally a dividend-escrow PV. Output: the observed spot path
//! that the flow/payoff nodes read.
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
    //! true only at date 0 (the spot today is fixed across all paths); used by the
    //! engine to skip re-evaluating that date per path.
    bool IsConstant( size_t DateIndex ) override;
    //! diffuse one log-Euler step (date > 0) or publish the spot today (date 0).
    void ComputeValue( size_t DateIndex ) override;
    //! declare the per-step inputs, including this node at DateIndex-1 (recursion).
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getters
    MonteCarloNode* GetLocalVolNode(); //!< per-step vol node
    MonteCarloNode* GetBrownianNode(); //!< Brownian W node
    MonteCarloNode* GetDriftNode();    //!< cumulative-carry drift node

    //! setters
    void SetLocalVolNode( MonteCarloNode* N ); //!< wire the (local/constant) vol
    void SetDriftNode( MonteCarloNode* N );    //!< wire the cumulative-carry drift
    void SetBrownianNode( MonteCarloNode* N ); //!< wire the Brownian increment
    void SetDividendNode( MonteCarloNode* N ); //!< optional discrete-dividend escrow
    void SetSpot( double Spot );               //!< set the spot today (also fills date 0)

    //! turn on the Milstein step, reading d(vol)/d(log spot) from the local-vol node
    void EnableMilstein( LocalVolatilityNode* Lv );

    SpotDiffusionNode( const string& Name );
    ~SpotDiffusionNode() override;
};
