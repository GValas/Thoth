#pragma once
#include "monte_carlo_node.hpp"

//! Placeholder yield-curve node: rates are currently wired as ConstantNodes (a flat
//! rate over the simulation), so this node is unused and its ComputeValue throws.
class YieldCurveNode : public MonteCarloNode
{

  private:
    map<date, double> _yield_map;

  public:
    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    YieldCurveNode( const string& Name );
    ~YieldCurveNode() override;
};
