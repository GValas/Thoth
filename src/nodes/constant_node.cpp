#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

ConstantNode::ConstantNode( const string& Name ) : MonteCarloNode( Name )
{
}

ConstantNode::~ConstantNode() = default;

//! GetValue already short-circuits to _constant_value, but keep the per-date slot
//! populated so generic value readers see a consistent _value_list
void ConstantNode::ComputeValue( size_t DateIndex )
{
    _value_list[DateIndex] = _constant_value;
}

void ConstantNode::SetConstantValue( double ConstantValue )
{
    _constant_value = ConstantValue;
}

//! constant on every date, so the scheduler evaluates it once instead of per path
bool ConstantNode::IsConstant( size_t /*DateIndex*/ )
{
    return true;
}

//! no children: a leaf of the DAG
void ConstantNode::GetDateDependencies( size_t /*DateIndex*/,
                                        vector<MonteCarloNode*>& /*NodeList*/,
                                        vector<size_t>& /*DateList*/ )
{
}
