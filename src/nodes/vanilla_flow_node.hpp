#pragma once
#include "monte_carlo_node.hpp"

//! Vanilla payoff at the maturity (flow) date — max(phi*(S_T - K), 0) for the
//! option type (phi = +1 call / -1 put); zero on every other date.
class VanillaFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node = nullptr;
    double _strike = 0;
    double _floor = 0;
    OptionType _type = OptionType::Call;
    size_t _flow_date_index = 0;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    size_t GetFlowDateIndex();
    MonteCarloNode* GetSpotNode();

    //! setter
    void SetStrike( double Strike );
    void SetFloor( double Floor );
    void SetType( OptionType Type );
    void SetFlowDateIndex( size_t DateIndex );
    void SetSpotNode( MonteCarloNode* N );

    VanillaFlowNode( const string& Name );
    ~VanillaFlowNode() override;
};
