#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

ConstantNode::ConstantNode( const string& Name ) : MonteCarloNode( Name )
{
}

ConstantNode::~ConstantNode() = default;

void ConstantNode::ComputeValue( size_t DateIndex )
{
    _value_list[DateIndex] = _constant_value;
}

void ConstantNode::SetConstantValue( double ConstantValue )
{
    _constant_value = ConstantValue;
}

bool ConstantNode::IsConstant( size_t /*DateIndex*/ )
{
    return true;
}

void ConstantNode::GetDateDependencies( size_t /*DateIndex*/,
                                        vector<MonteCarloNode*>& /*NodeList*/,
                                        vector<size_t>& /*DateList*/ )
{
}
