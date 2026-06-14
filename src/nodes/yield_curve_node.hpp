#pragma once
#include "monte_carlo_node.hpp"

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
