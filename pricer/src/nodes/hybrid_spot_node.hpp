#pragma once
#include "monte_carlo_node.hpp"

//! Hybrid (stochastic-rate) spot: the deterministic-carry diffusion multiplied by
//! exp( X(t) ), X the Hull-White exponent int_0^t x + V(t)/2. The wrapped
//! diffusion drifts at the deterministic projection carry z t; adding X makes the
//! effective log-drift int (r + spread - q) with r the stochastic short rate —
//! and the V/2 convexity keeps the forward E[D_T S_T]/P(0,T) exactly the
//! deterministic multi-curve forward. Wrapper pattern as QuantoAdjustmentNode:
//! it takes over the "<name>#spot" key so payoffs, barriers and recordings read
//! the hybrid spot transparently.
class HybridSpotNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node = nullptr;     //!< the deterministic-carry diffusion
    MonteCarloNode* _exponent_node = nullptr; //!< the HW exponent X(t)

  public:
    //! spot(t) * exp( X(t) ) (X(0) = 0, so date 0 is the plain spot)
    void ComputeValue( size_t DateIndex ) override;
    //! children: the wrapped spot and the exponent, both at this date
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setters
    void SetSpotNode( MonteCarloNode* N );
    void SetExponentNode( MonteCarloNode* N );

    HybridSpotNode( const string& Name );
    ~HybridSpotNode() override;
};
