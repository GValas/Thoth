#pragma once
#include "monte_carlo_node.hpp"

//! best-of / worst-of (rainbow) basket spot: the max (best) or min (worst) of the
//! member underlyings' rebased performances. Each member contributes
//! weight_i * S_i = (100 / S_i0) * S_i = 100 * S_i/S_i0, so at t0 every term is
//! 100 and the rainbow spot is 100 (a strike of 100 is at-the-money).
//!
//! Role in the MC node graph: a basket-spot combinator that a downstream flow node
//! prices like a single underlying. DAG inputs are the member spot nodes (read at
//! the same date); output is the selected rebased performance. Rebasing puts every
//! member on a common scale so best/worst is a fair comparison across underlyings
//! with different absolute spot levels. Invariant: _underlying_node_list and
//! _weight_list are parallel (same index, equal length).
class RainbowNode : public MonteCarloNode
{

  private:
    vector<MonteCarloNode*> _underlying_node_list; //!< member spot nodes, in push order
    vector<double> _weight_list;                   //!< 100 / S_i0 rebasing factor per member
    bool _best = true;                             //!< true = best-of (max), false = worst-of (min)

  public:
    //! select the max (best-of) or min (worst-of) rebased member performance.
    void ComputeValue( size_t DateIndex ) override;
    //! declare every member spot node at this date as a dependency.
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void PushUnderlying( MonteCarloNode* N ); //!< append a member spot node
    void PushWeight( double Weight );         //!< append its 100/S_i0 rebasing factor
    void SetBest( bool Best );                //!< true = best-of (max), false = worst-of (min)

    RainbowNode( const string& Name );
    ~RainbowNode() override;
};
