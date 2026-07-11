#include "thoth.hpp"
#include "nodes.hpp"

//************************************************************************/

//! Construct with no evaluator attached; the correlation factories wire it.
TermCorrelNode::TermCorrelNode( const string& Name ) : MonteCarloNode( Name )
{
}

TermCorrelNode::~TermCorrelNode() = default;

//! Populate the per-date correlation values. Side effect: fills _value_list and
//! sets _filled. The DateIndex argument is ignored: the values are deterministic,
//! so the whole vector is computed on the first call and every later call returns
//! immediately (the engine still invokes it once per date per path).
void TermCorrelNode::ComputeValue( size_t /*DateIndex*/ )
{
    if ( _filled )
    {
        return;
    }
    if ( !_evaluate )
    {
        ERR( "term correlation node '" + _name + "' : evaluator not set" );
    }
    //! schedule-wide setup first (e.g. factorise every step-average matrix once)
    if ( _prepare )
    {
        _prepare( _t_list );
    }
    for ( size_t i = 0; i < _date_list.size(); i++ )
    {
        _value_list[i] = _evaluate( i, _t_list[i] );
    }
    _filled = true;
}

//! wire the (date index, year fraction) -> value evaluator.
void TermCorrelNode::SetEvaluator( std::function<double( size_t, double )> Evaluate )
{
    _evaluate = std::move( Evaluate );
}

//! wire the optional one-shot schedule hook, run before the first fill.
void TermCorrelNode::SetPrepare( std::function<void( const vector<double>& )> Prepare )
{
    _prepare = std::move( Prepare );
}

//! Leaf node: its values come from the correlation object, not from other graph
//! nodes, so it declares no dependencies for the topological sort.
void TermCorrelNode::GetDateDependencies( size_t /*DateIndex*/,
                                          vector<MonteCarloNode*>& /*NodeList*/,
                                          vector<size_t>& /*DateList*/ )
{
}
