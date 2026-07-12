#pragma once
#include "monte_carlo_node.hpp"
#include "enums.hpp"

//! Monte-Carlo flow of an arithmetic average-price Asian option. At the maturity
//! date index it reads the spot at every averaging observation, forms the
//! arithmetic mean A, and pays nominal * max( omega*(A - K), 0 ); zero on every
//! other date. Path-dependent (reads the whole averaging window), like the
//! variance-swap flow.
class AsianFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node = nullptr; //!< underlying spot path
    double _strike = 0;                   //!< cash strike K
    OptionType _type = OptionType::Call;  //!< call (+1) / put (-1)
    double _notional = 1;
    size_t _flow_date_index = 0;            //!< maturity date index (the payment date)
    vector<size_t> _observation_date_index; //!< averaging fixings (sorted)

  public:
    //! at the flow date, average the spot over the schedule and emit the payoff
    void ComputeValue( size_t DateIndex ) override;
    //! at the flow date, depend on the spot at every averaging observation
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setters
    void SetSpotNode( MonteCarloNode* N );
    void SetStrike( double Strike );
    void SetType( OptionType Type );
    void SetNotional( double Notional );
    void SetFlowDateIndex( size_t DateIndex );
    void SetObservationDateIndices( const vector<size_t>& DateIndices );

    AsianFlowNode( const string& Name );
    ~AsianFlowNode() override;
};
