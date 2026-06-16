#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

CompositeVolNode::CompositeVolNode( const string& Name ) : MonteCarloNode( Name )
{
}

CompositeVolNode::~CompositeVolNode() = default;

void CompositeVolNode::ComputeValue( size_t DateIndex )
{
    //! v^2(SX) = v^2(S) + v^2(X) + 2 rho(S,X) v(S) v(X)
    double rho_sx = _rho_SX_node->GetValue( DateIndex );
    double vol_s = _vol_S_node->GetValue( DateIndex );
    double vol_x = _vol_X_node->GetValue( DateIndex );
    _value_list[DateIndex] = sqrt( vol_s * vol_s + vol_x * vol_x + 2 * rho_sx * vol_s * vol_x );
}

void CompositeVolNode::SetRhoSXNode( MonteCarloNode* N )
{
    _rho_SX_node = N;
}

void CompositeVolNode::SetVolSNode( MonteCarloNode* N )
{
    _vol_S_node = N;
}

void CompositeVolNode::SetVolXNode( MonteCarloNode* N )
{
    _vol_X_node = N;
}

MonteCarloNode* CompositeVolNode::GetRhoSXNode()
{
    return _rho_SX_node;
}

MonteCarloNode* CompositeVolNode::GetVolSNode()
{
    return _vol_S_node;
}

MonteCarloNode* CompositeVolNode::GetVolXNode()
{
    return _vol_X_node;
}

void CompositeVolNode::GetDateDependencies( size_t DateIndex,
                                            vector<MonteCarloNode*>& NodeList,
                                            vector<size_t>& DateList )
{
    NodeList.push_back( _rho_SX_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _vol_S_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _vol_X_node );
    DateList.push_back( DateIndex );
}
