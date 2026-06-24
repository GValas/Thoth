#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/
//! a node is identified by its (stable, unique) name — the ordering key that makes
//! the graph traversal and RNG draw order reproducible (see NodeNameLess)
MonteCarloNode::MonteCarloNode( const string& Name )
{
    _name = Name;
}

MonteCarloNode::~MonteCarloNode() = default;

const string& MonteCarloNode::GetName() const
{
    return _name;
}

//! default: a node is path-dependent (re-evaluated every path). ConstantNode and the
//! date-0 branch of the diffusion nodes override this to true.
bool MonteCarloNode::IsConstant( size_t /*DateIndex*/ )
{
    return false;
}

double MonteCarloNode::GetIndicatorValue( size_t DateIndex )
{
    //! sample count is per date index: a node scheduled at several dates must
    //! not divide a per-date sum by the total number of evaluations
    return _indicator_sum_list[DateIndex] / _indicator_count_list[DateIndex];
}

//! Monte-Carlo standard error of the mean estimator: sqrt( sample variance / n ),
//! where the sample variance uses the (sum2 - sum^2/n)/(n-1) one-pass form
double MonteCarloNode::GetIndicatorTrust( size_t DateIndex )
{
    const double n = _indicator_count_list[DateIndex];
    const double sum = _indicator_sum_list[DateIndex];
    return sqrt( ( _indicator_sum2_list[DateIndex] - sum * sum / n ) / ( n - 1 ) / n );
}

//! strict weak ordering on (node, date): order by node *name* (not heap address) then
//! by date index. Stable across runs, which keeps the topological sort — and thus the
//! RNG draw order — reproducible regardless of allocation addresses.
bool NodeNameLess::operator()( const node& a, const node& b ) const
{
    const string& na = a.first->GetName();
    const string& nb = b.first->GetName();
    if ( na != nb )
        return na < nb;
    return a.second < b.second;
}

//! collect this node's (child, date) dependencies for DateIndex into a de-duplicated,
//! name-ordered set (delegates to the virtual GetDateDependencies for the raw edges)
node_set MonteCarloNode::GetChildNodes( size_t DateIndex )
{
    node_set s;
    vector<MonteCarloNode*> node_list;
    vector<size_t> date_list;
    GetDateDependencies( DateIndex, node_list, date_list );
    for ( size_t i = 0;
          i < node_list.size();
          i++ )
    {
        s.insert( node( node_list[i], date_list[i] ) );
    }
    return s;
}

//! accumulate the running sum / sum-of-squares / count for this date — but only for
//! indicator nodes, so the hot diffusion loop pays nothing for intermediate nodes
void MonteCarloNode::UpdateIndicators( size_t DateIndex )
{
    if ( _is_indicator )
    {
        double x = _value_list[DateIndex];
        _indicator_sum_list[DateIndex] += x;
        _indicator_sum2_list[DateIndex] += x * x;
        _indicator_count_list[DateIndex] += 1;
    }
}

//! bind the diffusion schedule: size the per-date buffers and precompute the
//! year-fractions (dt, sqrt(dt), t) once — they are identical on every MC path
void MonteCarloNode::SetDateList( const vector<date>& DateList )
{
    _date_list = DateList;
    size_t n = _date_list.size();
    _value_list.resize( n );
    _indicator_sum_list.resize( n );
    _indicator_sum2_list.resize( n );
    _indicator_count_list.resize( n );

    //! precompute year-fractions once; these are identical on every MC path
    _dt_list.resize( n );
    _sqrt_dt_list.resize( n );
    _t_list.resize( n );
    for ( size_t i = 0; i < n; i++ )
    {
        //! t_i: year-fraction from the first date (used for cumulative drift / discounting)
        _t_list[i] = YearFraction( _date_list[0], _date_list[i] );
        if ( i > 0 )
        {
            //! dt_i / sqrt(dt_i): step length from the previous date (index 0 stays unused)
            _dt_list[i] = YearFraction( _date_list[i - 1], _date_list[i] );
            _sqrt_dt_list[i] = sqrt( _dt_list[i] );
        }
    }
}
