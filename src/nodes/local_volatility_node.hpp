#pragma once
#include "monte_carlo_node.hpp"

class LocalVolatilityNode : public MonteCarloNode
{

  private:
    //! per diffusion date: the local vol sampled on a log-spot grid, the grid
    //! spacing in log-spot, and the (signed) log-spot index of the grid's first
    //! point (so grid point i sits at log-spot = (offset + i) * ln_step).
    vector<la_vector*> _vol_vector_list;
    vector<double> _ln_step_list;
    vector<long> _offset_list;

    //! the spot path this surface is sampled along (reads the previous step)
    MonteCarloNode* _spot_node;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! d(local vol)/d(log spot) at the spot of the previous step, by central
    //! difference on the grid — the state-derivative the Milstein step needs
    double LogSpotDerivative( size_t DateIndex );

    void SetSpotNode( MonteCarloNode* SpotNode );
    void PushLnStep( double LnStep );
    void PushOffset( long Offset );
    void PushVolVector( la_vector* VolVector );

    LocalVolatilityNode( const string& Name );
    ~LocalVolatilityNode() override;
};
