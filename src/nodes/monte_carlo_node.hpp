#pragma once

class MonteCarloNode;
using node = pair<MonteCarloNode*, size_t>;

//! Order nodes by (name, date index) rather than by raw pointer, so the
//! topological sort — and therefore the order in which noise nodes draw from
//! the shared RNG — is reproducible and independent of heap addresses.
struct NodeNameLess
{
    bool operator()( const node& a, const node& b ) const;
};
using node_set = set<node, NodeNameLess>;

//! Base class for every Monte-Carlo node. Holds one value per diffusion date
//! (_value_list) and the precomputed schedule year-fractions; ComputeValue fills a
//! date's value from the node's children, GetDateDependencies declares those
//! children (so the collector can topologically sort the graph), and indicator
//! nodes (the book and contracts) also accumulate premium/trust statistics.
class MonteCarloNode
{
  protected:
    string _name;
    vector<date> _date_list;
    vector<double> _value_list;

    //! precomputed year-fractions (constant across all MC paths)
    vector<double> _dt_list;      // year-fraction from previous date (index 0 unused)
    vector<double> _sqrt_dt_list; // sqrt( _dt_list )
    vector<double> _t_list;       // year-fraction from _date_list[0]

    // indicators — opt-in: only nodes whose premium/trust is actually read
    // (the book and the contracts) accumulate, so the hot loop skips the
    // statistics for every intermediate diffusion node.
    bool _is_indicator = false;
    vector<double> _indicator_sum_list;   // container for sum, per date index
    vector<double> _indicator_sum2_list;  // container for sum2, per date index
    vector<double> _indicator_count_list; // number of samples accumulated, per date index

  public:
    //! constant
    //! true if the value is path-independent at this date (evaluated once, not per path)
    virtual bool IsConstant( size_t DateIndex );

    //! setter
    //! bind the diffusion schedule and precompute the per-date year-fractions
    void SetDateList( const vector<date>& DateList );

    //! getter
    const string& GetName() const;                //!< stable identifier / ordering key
    double GetIndicatorValue( size_t DateIndex ); //!< MC mean estimate at this date (sum / count)
    double GetIndicatorTrust( size_t DateIndex ); //!< MC standard error of that mean

    //! current path's value at this date; virtual so ConstantNode can short-circuit
    inline virtual double GetValue( size_t DateIndex )
    {
        return _value_list[DateIndex];
    }

    //! actions
    //! fold this path's value into the running statistics (no-op for non-indicators)
    void UpdateIndicators( size_t DateIndex );
    //! fill _value_list[DateIndex] from this node's children — the core per-node step
    virtual void ComputeValue( size_t DateIndex ) = 0;

    //! return (node, dates) mandatory for value computation
    //! declare the (child, date) edges needed to compute DateIndex (drives the topo sort)
    virtual void GetDateDependencies( size_t DateIndex,
                                      vector<MonteCarloNode*>& NodeList,
                                      vector<size_t>& DateList ) = 0;
    //! the same edges as a de-duplicated, name-ordered set
    node_set GetChildNodes( size_t DateIndex );

    //! constructor, destructor
    MonteCarloNode( const string& Name );
    virtual ~MonteCarloNode();
};
