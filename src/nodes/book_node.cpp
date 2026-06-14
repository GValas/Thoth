#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

BookNode::BookNode( const string& Name ) : MonteCarloNode( Name )
{
    _is_indicator = true; //!< the book premium/trust is reported
}

BookNode::~BookNode() = default;

void BookNode::ComputeValue( size_t DateIndex )
{
    double x = 0;
    for ( size_t i = 0;
          i < _contract_node_list.size();
          i++ )
    {
        x += _contract_node_list[i]->GetValue( DateIndex ) *
             _forex_node_list[i]->GetValue( DateIndex );
    }
    _value_list[DateIndex] = x;
}

void BookNode::PushContractNode( MonteCarloNode* N )
{
    _contract_node_list.push_back( N );
}

void BookNode::PushForexNode( MonteCarloNode* N )
{
    _forex_node_list.push_back( N );
}

vector<MonteCarloNode*> BookNode::GetContractNodeList()
{
    return _contract_node_list;
}

vector<MonteCarloNode*> BookNode::GetForexNodeList()
{
    return _forex_node_list;
}

void BookNode::GetDateDependencies( size_t DateIndex,
                                    vector<MonteCarloNode*>& NodeList,
                                    vector<size_t>& DateList )
{
    for ( size_t i = 0;
          i < _contract_node_list.size();
          i++ )
    {
        NodeList.push_back( _contract_node_list[i] );
        DateList.push_back( DateIndex );
        NodeList.push_back( _forex_node_list[i] );
        DateList.push_back( DateIndex );
    }
}
