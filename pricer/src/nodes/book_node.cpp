#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

BookNode::BookNode( const string& Name ) : MonteCarloNode( Name )
{
    _is_indicator = true; //!< the book premium/trust is reported
}

BookNode::~BookNode() = default;

//! book premium for the date: sum over contracts of (contract premium * FX factor),
//! all expressed in the book currency. As an indicator node this value is what the
//! reporting layer reads (mean + trust) for the whole book.
void BookNode::ComputeValue( size_t DateIndex )
{
    double x = 0;
    for ( size_t i = 0;
          i < _contract_node_list.size();
          i++ )
    {
        //! FX factor to the book currency: 1 when no forex node is attached
        //! (same-currency book — the common case, no node created)
        double fx = ( i < _forex_node_list.size() && _forex_node_list[i] )
                        ? _forex_node_list[i]->GetValue( DateIndex )
                        : 1.0;
        x += _contract_node_list[i]->GetValue( DateIndex ) * fx;
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
//! the book at DateIndex reads each contract premium at DateIndex, and the FX node
//! at DateIndex when one is attached for that contract (same-currency legs have none)
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
        //! only declare the FX child when it exists, mirroring the factor-1 fallback in ComputeValue
        if ( i < _forex_node_list.size() && _forex_node_list[i] )
        {
            NodeList.push_back( _forex_node_list[i] );
            DateList.push_back( DateIndex );
        }
    }
}
