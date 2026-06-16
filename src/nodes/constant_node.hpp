#pragma once
#include "monte_carlo_node.hpp"

//! A node whose value is the same constant on every date (a rate, a vol, an FX spot
//! or a Cholesky coefficient). IsConstant() is true, so the scheduler evaluates it
//! once instead of per path.
class ConstantNode : public MonteCarloNode
{

  private:
    double _constant_value;

  public:
    bool IsConstant( size_t DateIndex ) override;

    inline double GetValue( size_t /*DateIndex*/ ) override
    {
        return _constant_value;
    }

    void ComputeValue( size_t DateIndex ) override;
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetConstantValue( double ConstantValue );

    ConstantNode( const string& Name );
    ~ConstantNode() override;
};
