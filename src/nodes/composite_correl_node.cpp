#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

CompositeCorrelNode::CompositeCorrelNode( const string& Name ) : MonteCarloNode( Name )
{
}

CompositeCorrelNode::~CompositeCorrelNode() = default;

void CompositeCorrelNode::ComputeValue( size_t DateIndex )
{
    // rho(S.IJ, AB) = ( rho(S,AB).v(S) + rho(IJ,AB).v(IJ) ) / v(S.IJ)
    // v�(S.IJ) = v�(IJ) + v�(S) + 2.rho(S,IJ).v(S).v(IJ)
    double rho_s_ab = _rho_S_AB_node->GetValue( DateIndex );
    double rho_ij_ab = _rho_IJ_AB_node->GetValue( DateIndex );
    double vol_ij = _vol_IJ_node->GetValue( DateIndex );
    double vol_s = _vol_S_node->GetValue( DateIndex );
    double vol_s_ij = _vol_S_IJ_node->GetValue( DateIndex );
    _value_list[DateIndex] = ( rho_s_ab * vol_s + rho_ij_ab * vol_ij ) / vol_s_ij;
}

//! setter
void CompositeCorrelNode::SetRhoSABNode( MonteCarloNode* N )
{
    _rho_S_AB_node = N;
}

void CompositeCorrelNode::SetRhoIJABNode( MonteCarloNode* N )
{
    _rho_IJ_AB_node = N;
}

void CompositeCorrelNode::SetVolSNode( MonteCarloNode* N )
{
    _vol_S_node = N;
}

void CompositeCorrelNode::SetVolIJNode( MonteCarloNode* N )
{
    _vol_IJ_node = N;
}

void CompositeCorrelNode::SetVolSIJNode( MonteCarloNode* N )
{
    _vol_S_IJ_node = N;
}

MonteCarloNode* CompositeCorrelNode::GetRhoSABNode()
{
    return _rho_S_AB_node;
}

MonteCarloNode* CompositeCorrelNode::GetRhoIJABNode()
{
    return _rho_IJ_AB_node;
}

MonteCarloNode* CompositeCorrelNode::GetVolSnode()
{
    return _vol_S_node;
}

MonteCarloNode* CompositeCorrelNode::GetVolIJNode()
{
    return _vol_IJ_node;
}

MonteCarloNode* CompositeCorrelNode::GetVolSIJNode()
{
    return _vol_S_IJ_node;
}

void CompositeCorrelNode::GetDateDependencies( size_t DateIndex,
                                               vector<MonteCarloNode*>& NodeList,
                                               vector<size_t>& DateList )
{
    NodeList.push_back( _rho_S_AB_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _rho_IJ_AB_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _vol_S_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _vol_IJ_node );
    DateList.push_back( DateIndex );
    NodeList.push_back( _vol_S_IJ_node );
    DateList.push_back( DateIndex );
}
