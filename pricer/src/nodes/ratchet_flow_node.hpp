#pragma once
#include "monte_carlo_node.hpp"

//! Monte-Carlo flow of a ratchet (cliquet) note. At the maturity date index it
//! reads the spot at every period boundary, forms each period return
//! R_i = S(t_i)/S(t_{i-1}) - 1 clipped to [local_floor, local_cap], sums them,
//! clips the sum to [global_floor, global_cap] and pays nominal * that coupon.
//! Zero on every other date.
class RatchetFlowNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _spot_node = nullptr; //!< underlying spot path
    double _notional = 100;
    size_t _flow_date_index = 0;            //!< maturity date index (the payment date)
    vector<size_t> _observation_date_index; //!< period boundaries (sorted; [0] = today)
    double _local_floor = 0;                //!< per-period return floor / cap (decimals)
    double _local_cap = 0;
    double _global_floor = 0; //!< coupon floor / cap (decimals)
    double _global_cap = 0;
    bool _has_global_cap = false;

  public:
    //! at the flow date, sum the clipped period returns and emit the coupon
    void ComputeValue( size_t DateIndex ) override;
    //! at the flow date, depend on the spot at every boundary
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setters
    void SetSpotNode( MonteCarloNode* N );
    void SetNotional( double Notional );
    void SetFlowDateIndex( size_t DateIndex );
    void SetObservationDateIndices( const vector<size_t>& DateIndices );
    void SetLocalClip( double Floor, double Cap );
    void SetGlobalClip( double Floor, bool HasCap, double Cap );

    RatchetFlowNode( const string& Name );
    ~RatchetFlowNode() override;
};
