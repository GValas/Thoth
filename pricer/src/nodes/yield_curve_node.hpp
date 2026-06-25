#pragma once
#include "monte_carlo_node.hpp"

class Curve;

//! Term-structured zero-rate node: at each diffusion date it returns the curve's
//! continuously-compounded zero rate to that date (the rho bump baked into the
//! curve's GetCurveValue is included). Replaces the former flat front-pillar
//! ConstantNode so the Monte-Carlo drift and discounting follow the whole curve
//! rather than a single rate. The values are path-independent, so the node fills its
//! entire vector once on the first evaluation and is a no-op afterwards.
//!
//! Consumers: the DriftNode subtracts these zero rates leg by leg, and the spot
//! diffusion turns the cumulative net carry into a per-step forward drift (see
//! SpotDiffusionNode); the ContractNode discounts with the same zero rates. For a
//! flat curve every zero rate equals the flat rate, so both reduce exactly to the
//! previous constant-rate behaviour.
class YieldCurveNode : public MonteCarloNode
{

  private:
    Curve* _curve = nullptr; //!< the zero curve sampled per diffusion date (non-owning)
    bool _filled = false;    //!< true once the whole value vector has been populated
    //! the curve's rho shift, snapshotted at build time. The single-tree rho bump shifts
    //! the curve, builds this (suffixed) node, then RESTORES the shift before the path
    //! sweep — but ComputeValue runs during the sweep, when the live shift is back to 0.
    //! Freezing it here lets the bumped node still discount / drift at the bumped rate
    //! (base and non-rate nodes snapshot 0, so they are unaffected). Fixes MCL rho.
    double _shift = 0.0;

  public:
    //! fill the entire zero-rate vector on first call, then no-op (path-independent).
    void ComputeValue( size_t DateIndex ) override;
    //! leaf node (reads the curve, not other nodes): declares no dependencies.
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter: the curve whose zero rates this node samples per diffusion date
    void SetCurve( Curve* C );

    YieldCurveNode( const string& Name );
    ~YieldCurveNode() override;
};
