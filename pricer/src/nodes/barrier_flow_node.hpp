#pragma once
#include "monte_carlo_node.hpp"

//! Barrier option payoff for one path: at the flow date it pays the vanilla payoff
//! only if the contract is "alive" — knock-in and the barrier was touched on a
//! monitored date, or knock-out and never touched (up- or down-barrier per _is_up).
class BarrierFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node; //!< spot path read both for monitoring and for the terminal payoff
    double _strike;             //!< vanilla strike of the embedded payoff
    double _floor;              //!< payoff floor (capped/floored payoff support)
    OptionType _type;           //!< call / put of the embedded vanilla
    size_t _flow_date_index;    //!< diffusion-date index at which the payoff is paid (maturity)

    double _barrier_level;              //!< already continuity-corrected (discrete-monitoring shift baked in)
    bool _is_up;                        //!< up- vs down-barrier (hit test is s>=H vs s<=H)
    bool _is_in;                        //!< knock-in vs knock-out (selects the "alive" condition)
    vector<size_t> _monitor_index_list; //!< diffusion dates on which the barrier is checked

  public:
    //! pays the (possibly floored) vanilla payoff at the flow date iff the barrier
    //! condition leaves the option alive; 0 on every other date and when knocked dead
    void ComputeValue( size_t DateIndex ) override;
    //! at the flow date, depends on the spot at every monitor date plus the spot at
    //! the flow date itself; no dependencies on any other date
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetStrike( double Strike );                           //!< embedded vanilla strike
    void SetFloor( double Floor );                             //!< payoff floor passed to payoff_vanilla
    void SetType( OptionType Type );                           //!< call / put
    void SetFlowDateIndex( size_t DateIndex );                 //!< maturity / payment date index
    void SetSpotNode( MonteCarloNode* N );                     //!< underlying spot path
    void SetBarrierLevel( double Level );                      //!< continuity-corrected barrier H
    void SetIsUp( bool IsUp );                                 //!< up-barrier (true) vs down-barrier
    void SetIsIn( bool IsIn );                                 //!< knock-in (true) vs knock-out
    void SetMonitorIndexList( const vector<size_t>& Indices ); //!< the dates to test the barrier on

    BarrierFlowNode( const string& Name );
    ~BarrierFlowNode() override;
};
