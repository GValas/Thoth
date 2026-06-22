#pragma once
#include "monte_carlo_node.hpp"

//! Vanilla payoff at the maturity (flow) date — max(phi*(S_T - K), 0) for the
//! option type (phi = +1 call / -1 put); zero on every other date.
//!
//! Role in the MC node graph: a payoff leaf for the contract. It reads the diffused
//! spot at the single maturity (flow) date and emits the option intrinsic there; on
//! all other dates it emits 0, so the downstream discount/accumulation only picks up
//! cash at maturity. DAG input: the spot node (only at the flow date). The optional
//! floor caps the payoff below at a configured level.
class VanillaFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node = nullptr; //!< underlying spot path
    double _strike = 0;                   //!< strike K
    double _floor = 0;                    //!< payoff floor (lower bound)
    OptionType _type = OptionType::Call;  //!< call (phi=+1) or put (phi=-1)
    size_t _flow_date_index = 0;          //!< date index at which the option pays

  public:
    //! emit the vanilla intrinsic at the flow date, 0 otherwise.
    void ComputeValue( size_t DateIndex ) override;
    //! depend on the spot only at the flow date (no cash flows elsewhere).
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter

    //! setter
    void SetStrike( double Strike );           //!< set K
    void SetFloor( double Floor );             //!< set the payoff floor
    void SetType( OptionType Type );           //!< call / put
    void SetFlowDateIndex( size_t DateIndex ); //!< set the maturity date index
    void SetSpotNode( MonteCarloNode* N );     //!< wire the underlying spot

    VanillaFlowNode( const string& Name );
    ~VanillaFlowNode() override;
};
