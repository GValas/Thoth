#pragma once
#include "monte_carlo_node.hpp"

//! best-of / worst-of (rainbow) basket spot: the max (best) or min (worst) of the
//! member underlyings' rebased performances. Each member contributes
//! weight_i * S_i = (100 / S_i0) * S_i = 100 * S_i/S_i0, so at t0 every term is
//! 100 and the rainbow spot is 100 (a strike of 100 is at-the-money).
class RainbowNode : public MonteCarloNode
{

  private:
    vector<MonteCarloNode*> _underlying_node_list;
    vector<double> _weight_list; //!< 100 / S_i0 rebasing factor per member
    bool _best = true;           //!< true = best-of (max), false = worst-of (min)

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void PushUnderlying( MonteCarloNode* N );
    void PushWeight( double Weight );
    void SetBest( bool Best );

    RainbowNode( const string& Name );
    ~RainbowNode() override;
};
