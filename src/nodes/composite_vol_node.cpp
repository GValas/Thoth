#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

CompositeVolNode::CompositeVolNode( const string& Name ) : MonteCarloNode( Name )
{
}

CompositeVolNode::~CompositeVolNode() = default;

//! the quanto/composite product S*X has log-return log S + log X, whose variance is
//! the sum of variances plus twice the covariance — the standard "vol of a product":
//!   v^2(SX) = v^2(S) + v^2(X) + 2 rho(S,X) v(S) v(X)
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
//! the composite vol reads the correlation and both component vols at the same date
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
