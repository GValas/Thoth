#pragma once
#include "monte_carlo_node.hpp"

//! Quanto adjustment: multiplies the diffused spot by exp(-rho(S,FX) sigma_S
//! sigma_FX t), the change-of-measure drift correction applied to an asset whose
//! payoff is settled in a foreign currency.
//!
//! Role in the MC node graph: a per-date transform of an already-diffused spot.
//! When pricing a foreign-currency asset in the domestic measure, the asset's
//! drift picks up an extra -rho * sigma_S * sigma_FX term; this node bakes the
//! cumulative effect of that term over [0, t] into a deterministic multiplier on
//! the spot. DAG inputs (all read at the same date): the underlying spot, the
//! underlying vol, the FX vol and the underlying/FX correlation. Output: the
//! quanto-adjusted spot. Invariant: at t = 0 the multiplier is 1 (zero horizon).
class QuantoAdjustmentNode : public MonteCarloNode
{

  private:
    MonteCarloNode* _udl_vol_node;       //!< sigma_S of the underlying
    MonteCarloNode* _fx_vol_node;        //!< sigma_FX of the settlement FX rate
    MonteCarloNode* _udl_fx_correl_node; //!< rho(S, FX)
    MonteCarloNode* _udl_spot_node;      //!< the diffused spot being adjusted

  public:
    //! apply the quanto multiplier exp(-rho sigma_S sigma_FX t) to the spot.
    void ComputeValue( size_t DateIndex ) override;
    //! declare the four inputs (spot, vols, correlation) at this date.
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter

    //! setter
    void SetUdlVolNode( MonteCarloNode* N );      //!< wire sigma_S
    void SetFxVolNode( MonteCarloNode* N );       //!< wire sigma_FX
    void SetUdlFxCorrelNode( MonteCarloNode* N ); //!< wire rho(S, FX)
    void SetUdlSpotNode( MonteCarloNode* N );     //!< wire the spot to adjust

    QuantoAdjustmentNode( const string& Name );
    ~QuantoAdjustmentNode() override;
};
