#pragma once
#include "monte_carlo_node.hpp"

//! A node whose value is the same constant on every date (a rate, a vol, an FX spot
//! or a Cholesky coefficient). IsConstant() is true, so the scheduler evaluates it
//! once instead of per path.
class ConstantNode : public MonteCarloNode
{

  private:
    double _constant_value; //!< the single value returned on every date

  public:
    //! always true: the value is path- and date-independent
    bool IsConstant( size_t DateIndex ) override;

    //! short-circuit the per-date storage: return the constant directly regardless of the date
    inline double GetValue( size_t /*DateIndex*/ ) override
    {
        return _constant_value;
    }

    //! write the constant into the date slot (kept for the uniform evaluate path)
    void ComputeValue( size_t DateIndex ) override;
    //! a constant has no children — nothing to declare
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! setter
    void SetConstantValue( double ConstantValue ); //!< set the constant (rate, vol, FX spot, Cholesky coeff)

    ConstantNode( const string& Name );
    ~ConstantNode() override;
};
