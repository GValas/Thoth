#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

ProductNode::ProductNode( const string& Name ) : MonteCarloNode( Name )
{
}

ProductNode::~ProductNode() = default;
//! append a factor child and its exponent, keeping the two parallel lists aligned.
void ProductNode::PushNode( MonteCarloNode* N,
                            double Pow )
{
    _node_list.push_back( N );
    _pow_list.push_back( Pow );
}

//! Fold the children into prod_i base_i^p_i for this date. Side effect: writes
//! _value_list[DateIndex]. Starts the accumulator at the multiplicative identity 1.
void ProductNode::ComputeValue( size_t DateIndex )
{
    double x = 1;
    for ( size_t i = 0;
          i < _node_list.size();
          i++ )
    {
        double base = _node_list[i]->GetValue( DateIndex );
        double p = _pow_list[i];
        //! avoid the cost of generic pow() for the common integer/simple exponents
        if ( p == 1.0 )
            x *= base;
        else if ( p == 2.0 )
            x *= base * base;
        else if ( p == 0.5 )
            x *= sqrt( base );
        else if ( p == -1.0 )
            x /= base;
        else
            x *= pow( base, p );
    }
    _value_list[DateIndex] = x;
}

//! Declare every factor child at the same date: the product is a pointwise
//! combine, so each base is needed at this date index.
void ProductNode::GetDateDependencies( size_t DateIndex,
                                       vector<MonteCarloNode*>& NodeList,
                                       vector<size_t>& DateList )
{
    for ( auto i : _node_list )
    {
        NodeList.push_back( i );
        DateList.push_back( DateIndex );
    }
}
