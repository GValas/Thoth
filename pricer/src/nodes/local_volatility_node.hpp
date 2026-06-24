#pragma once
#include "monte_carlo_node.hpp"
#include "raii.hpp"

//! Local (Dupire) volatility along the path: looks up sigma_loc(S_{i-1}, t_i) on a
//! precomputed per-date log-spot grid by clamped linear interpolation. Built from a
//! SABR (or other) implied surface — see Single::BuildLocalVolNode.
class LocalVolatilityNode : public MonteCarloNode
{

  private:
    //! per diffusion date: the local vol sampled on a log-spot grid, the grid
    //! spacing in log-spot, and the (signed) log-spot index of the grid's first
    //! point (so grid point i sits at log-spot = (offset + i) * ln_step). The
    //! vectors are owned here (RAII: freed when the node is destroyed).
    vector<LaVector> _vol_vector_list;
    vector<double> _ln_step_list;
    vector<long> _offset_list;

    //! the spot path this surface is sampled along (reads the previous step)
    MonteCarloNode* _spot_node;

  public:
    //! sigma_loc(S_{i-1}, t_i): clamped linear interpolation on this date's log-spot grid
    void ComputeValue( size_t DateIndex ) override;
    //! depends only on the spot of the previous step (the state the surface is sampled at)
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! d(local vol)/d(log spot) at the spot of the previous step, by central
    //! difference on the grid — the state-derivative the Milstein step needs
    double LogSpotDerivative( size_t DateIndex );

    void SetSpotNode( MonteCarloNode* SpotNode ); //!< wire the spot path the surface is read along
    void PushLnStep( double LnStep );             //!< append a date's log-spot grid spacing (build order)
    void PushOffset( long Offset );               //!< append a date's grid offset (first point's log-spot index)
    void PushVolVector( la_vector* VolVector );   //!< append a date's sampled local-vol vector (ownership taken)

    LocalVolatilityNode( const string& Name );
    ~LocalVolatilityNode() override;
};
