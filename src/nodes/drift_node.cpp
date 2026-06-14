#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

DriftNode::DriftNode( const string& Name ) : MonteCarloNode( Name )
{
    _domestic_rate_node = nullptr;
    _foreign_rate_node = nullptr;
    _repo_node = nullptr;
    _dividend_node = nullptr;
}

DriftNode::~DriftNode() = default;

void DriftNode::ComputeValue( size_t DateIndex )
{
    _value_list[DateIndex] =
        ( _domestic_rate_node ? _domestic_rate_node->GetValue( DateIndex ) : 0 ) -
        ( _foreign_rate_node ? _foreign_rate_node->GetValue( DateIndex ) : 0 ) -
        ( _repo_node ? _repo_node->GetValue( DateIndex ) : 0 ) -
        ( _dividend_node ? _dividend_node->GetValue( DateIndex ) : 0 );
}

void DriftNode::SetDomesticRateNode( MonteCarloNode* N )
{
    _domestic_rate_node = N;
}

void DriftNode::SetForeignRateNode( MonteCarloNode* N )
{
    _foreign_rate_node = N;
}

void DriftNode::SetRepoRateNode( MonteCarloNode* N )
{
    _repo_node = N;
}

void DriftNode::SetDividendRateNode( MonteCarloNode* N )
{
    _dividend_node = N;
}

MonteCarloNode* DriftNode::GetDomesticRateNode()
{
    return _domestic_rate_node;
}

MonteCarloNode* DriftNode::GetForeignRateNode()
{
    return _foreign_rate_node;
}

MonteCarloNode* DriftNode::GetRepoNode()
{
    return _repo_node;
}

MonteCarloNode* DriftNode::GetDividendNode()
{
    return _dividend_node;
}

void DriftNode::GetDateDependencies( size_t DateIndex,
                                     vector<MonteCarloNode*>& NodeList,
                                     vector<size_t>& DateList )
{
    if ( _domestic_rate_node )
    {
        NodeList.push_back( _domestic_rate_node );
        DateList.push_back( DateIndex );
    }
    if ( _foreign_rate_node )
    {
        NodeList.push_back( _foreign_rate_node );
        DateList.push_back( DateIndex );
    }
    if ( _repo_node )
    {
        NodeList.push_back( _repo_node );
        DateList.push_back( DateIndex );
    }
    if ( _dividend_node )
    {
        NodeList.push_back( _dividend_node );
        DateList.push_back( DateIndex );
    }
}
