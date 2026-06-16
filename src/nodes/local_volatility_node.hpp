#pragma once
#include "monte_carlo_node.hpp"

class LocalVolatilityNode : public MonteCarloNode
{

  private:
    //! gsl interpolation stuff
    // gsl_interp_accel * _g_acc;
    // gsl_spline * _g_spline;

    vector<la_vector*> _vol_vector_list;
    vector<double> _ln_step_list;
    vector<size_t> _offset_list;

    //! for fast interpolation
    MonteCarloNode* _spot_node;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    void SetSpotNode( MonteCarloNode* SpotNode );
    void PushLnStep( double LnStep );
    void PushOffset( size_t Offset );
    void PushVolVector( la_vector* VolVector );

    LocalVolatilityNode( const string& Name );
    ~LocalVolatilityNode() override;
};
