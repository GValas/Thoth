#pragma once
#include "monte_carlo_node.hpp"

//! Product of its child node values raised to per-child powers, prod_i child_i^p_i
//! (e.g. spot * FX for a composite; integer / simple powers are special-cased).
//!
//! Role in the MC node graph: a pure combinator. DAG inputs are the pushed child
//! nodes (read at the same date index); output is their weighted geometric
//! product. The classic use is building a composite/quanto spot S_dom = S * FX, or
//! a ratio via a -1 power. Invariant: _node_list and _pow_list grow together, so
//! they share an index and have equal length.
class ProductNode : public MonteCarloNode
{
  private:
    vector<MonteCarloNode*> _node_list; //!< the factor (child) nodes, in push order
    vector<double> _pow_list;           //!< exponent for each child, same index as _node_list

  public:
    //! evaluate prod_i child_i(DateIndex)^p_i and store it for this date.
    void ComputeValue( size_t DateIndex ) override;
    //! declare every factor child as a same-date dependency for the topo sort.
    void GetDateDependencies( size_t DateIndex,
                              vector<MonteCarloNode*>& NodeList,
                              vector<size_t>& DateList ) override;

    //! getter
    vector<MonteCarloNode*> GetNodeList(); //!< the factor children (copy)

    //! setter
    //! append a factor child N with exponent Pow (paired into the two parallel lists)
    void PushNode( MonteCarloNode* N,
                   double Pow );

    ProductNode( const string& Name );
    ~ProductNode() override;
};
