#pragma once
#include "monte_carlo_node.hpp"

class BarrierFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node;
    double _strike;
    double _floor;
    OptionType _type;
    size_t _flow_date_index;

    double _barrier_level;              //!< already continuity-corrected
    bool _is_up;                        //!< up- vs down-barrier
    bool _is_in;                        //!< knock-in vs knock-out
    vector<size_t> _monitor_index_list; //!< diffusion dates to monitor

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetStrike( double Strike );
    void SetFloor( double Floor );
    void SetType( OptionType Type );
    void SetFlowDateIndex( size_t DateIndex );
    void SetSpotNode( MonteCarloNode* N );
    void SetBarrierLevel( double Level );
    void SetIsUp( bool IsUp );
    void SetIsIn( bool IsIn );
    void SetMonitorIndexList( const vector<size_t>& Indices );

    BarrierFlowNode( const string& Name );
    ~BarrierFlowNode() override;
};
