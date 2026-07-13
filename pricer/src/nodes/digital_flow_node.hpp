#pragma once
#include "monte_carlo_node.hpp"

//! Digital (binary) payoff at the maturity (flow) date: cash-or-nothing pays a fixed
//! cash amount, asset-or-nothing pays the terminal spot, in both cases iff the option is
//! in the money (S_T > K for a call, S_T < K for a put); zero on every other date.
//!
//! Role in the MC node graph: a payoff leaf for a Digital contract. It reads the diffused
//! spot at the single maturity date and emits the binary payoff there; on all other dates
//! it emits 0, so the downstream discount/accumulation only picks up cash at maturity.
//! DAG input: the spot node (only at the flow date).
class DigitalFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node = nullptr; //!< underlying spot path
    double _strike = 0;                   //!< strike K
    OptionType _type = OptionType::Call;  //!< call (S>K) or put (S<K)
    bool _is_cash = true;                 //!< cash-or-nothing vs asset-or-nothing
    double _cash_amount = 1;              //!< fixed cash payout Q (cash-or-nothing only)
    size_t _flow_date_index = 0;          //!< date index at which the option pays

  public:
    //! emit the binary payoff at the flow date, 0 otherwise.
    void ComputeValue( size_t DateIndex ) override;
    //! depend on the spot only at the flow date (no cash flows elsewhere).
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetStrike( double Strike );           //!< set K
    void SetType( OptionType Type );           //!< call / put
    void SetCashOrNothing( bool IsCash );      //!< cash-or-nothing vs asset-or-nothing
    void SetCashAmount( double Amount );       //!< set the fixed cash payout Q
    void SetFlowDateIndex( size_t DateIndex ); //!< set the maturity date index
    void SetSpotNode( MonteCarloNode* N );     //!< wire the underlying spot

    DigitalFlowNode( const string& Name );
    ~DigitalFlowNode() override;
};
