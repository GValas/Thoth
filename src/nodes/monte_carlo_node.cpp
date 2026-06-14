#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/
MonteCarloNode::MonteCarloNode( const string& Name )
{
    _name = Name;
}

MonteCarloNode::~MonteCarloNode() = default;

const string& MonteCarloNode::GetName() const
{
    return _name;
}

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

double MonteCarloNode::GetIndicatorTrust( size_t DateIndex )
{
    const double n = _indicator_count_list[DateIndex];
    const double sum = _indicator_sum_list[DateIndex];
    return sqrt( ( _indicator_sum2_list[DateIndex] - sum * sum / n ) / ( n - 1 ) / n );
}

bool NodeNameLess::operator()( const node& a, const node& b ) const
{
    const string& na = a.first->GetName();
    const string& nb = b.first->GetName();
    if ( na != nb )
        return na < nb;
    return a.second < b.second;
}

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
        _t_list[i] = YearFraction( _date_list[0], _date_list[i] );
        if ( i > 0 )
        {
            _dt_list[i] = YearFraction( _date_list[i - 1], _date_list[i] );
            _sqrt_dt_list[i] = sqrt( _dt_list[i] );
        }
    }
}
