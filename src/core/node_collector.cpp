//! node_collector.cpp — owns the Monte-Carlo node DAG and drives its evaluation.
//! Responsibilities: own the nodes (by name), hold the diffusion-date schedule,
//! topologically sort the graph into an evaluation order (SortNodes), evaluate it
//! per path (PriceNodes), record selected node values per path for the American /
//! Longstaff-Schwartz pass, and emit the graph as Graphviz for debugging.
#include "thoth.hpp"
#include "node_collector.hpp"
#include <fstream>

//! constructor — empty graph; the pricer fills it via SetDiffusionDates + node builders.
NodeCollector::NodeCollector() = default;

//! destructor
NodeCollector::~NodeCollector() = default; //!< unique_ptr in _node_map frees the nodes

//! drop every node and cached date structure, returning the collector to its
//! freshly-constructed state so the owning pricer can rebuild the graph
void NodeCollector::Reset()
{
    _diffusion_dates.clear();
    _date_list.clear();
    _previous_diffusion_dates.clear();
    _index_date_map.clear();
    _node_map.clear(); //!< frees every owned node (unique_ptr)
    _brownian_node_map.clear();
    _node_list.clear();
    _date_index_list.clear();
    _records.clear();
    _scenario_suffix.clear();
    _scenario_bumps_rate = false;
    _scenario_bumps_vol = false;
}

//! push back — register a Brownian node. It is doubly indexed: the non-owning
//! _brownian_node_map gives typed access to the noise leaves, while PushNode takes
//! ownership in the main _node_map like any other node.
void NodeCollector::PushBrownianNode( std::unique_ptr<BrownianNode> N )
{
    _brownian_node_map[N->GetName()] = N.get(); //!< non-owning view (capture before move)
    PushNode( std::move( N ) );
}

//! push back — adopt a node into the owning map. Hand it the diffusion-date schedule
//! up front so it can size/precompute per-date state before evaluation.
void NodeCollector::PushNode( std::unique_ptr<MonteCarloNode> N )
{
    N->SetDateList( _date_list );
    _node_map[N->GetName()] = std::move( N ); //!< adopt ownership
}

//! setter — install the diffusion schedule and derive the lookup structures the
//! nodes need. The set is sorted ascending, so today is its first element.
void NodeCollector::SetDiffusionDates( const set<date>& DiffusionDates )
{
    _diffusion_dates = DiffusionDates;
    _today = *DiffusionDates.begin(); //!< earliest date = valuation date

    //! single pass over the ascending dates to build, in lockstep:
    //!  - _date_list:           index -> date (the array nodes step over)
    //!  - _index_date_map:      date  -> index (reverse lookup)
    //!  - _previous_diffusion_dates: date -> immediately preceding date
    set<date>::iterator d;
    date previous_date;
    size_t i = 0;
    for ( d = _diffusion_dates.begin();
          d != _diffusion_dates.end();
          d++ )
    {
        _date_list.push_back( *d );
        _index_date_map[*d] = i++;

        //! today has no predecessor; every later date maps to the one before it,
        //! which a diffusion node uses to read its own previous value (Δt steps).
        if ( *d != _today )
        {
            _previous_diffusion_dates[*d] = previous_date;
        }
        previous_date = *d; //!< carry forward for the next iteration
    }
}

//! brownian is special : also registered in the non-owning _brownian_node_map.
//! Mirrors the NewNode<T> template but routes through PushBrownianNode for the
//! extra index. The scenario suffix is appended so a Greek bump can build its own
//! copy (though Brownian leaves are usually shared via their bare name).
BrownianNode* NodeCollector::NewBrownianNode( const string& Name )
{
    auto n = std::make_unique<BrownianNode>( Name + _scenario_suffix );
    BrownianNode* p = n.get(); //!< observer pointer to return after the move
    PushBrownianNode( std::move( n ) );
    return p;
}

//! getter — nullptr when absent (the node-or-create callers rely on this);
//! must not insert, or a typo'd name would pollute the owning map with a null
MonteCarloNode* NodeCollector::GetNode( const string& Name )
{
    auto it = _node_map.find( Name );
    return it == _node_map.end() ? nullptr : it->second.get();
}

//! getter — nullptr when absent (non-owning view), no insertion on miss
BrownianNode* NodeCollector::GetBrownianNode( const string& Name )
{
    auto it = _brownian_node_map.find( Name );
    return it == _brownian_node_map.end() ? nullptr : it->second;
}

//! get previous diffusion date (the date must be a known diffusion date).
//! Errors rather than returning a sentinel: a miss means a node asked for a date
//! outside the schedule, which is a graph-construction bug worth surfacing.
date NodeCollector::PreviousDiffusionDate( const date& AsOfDate )
{
    auto it = _previous_diffusion_dates.find( AsOfDate );
    if ( it == _previous_diffusion_dates.end() )
    {
        ERR( "no previous diffusion date for " + to_simple_string( AsOfDate ) );
    }
    return it->second;
}

//! ----------------------------------------------------------------------
//! per-path recording (feeds the American / Longstaff-Schwartz pricer)
//! ----------------------------------------------------------------------

//! register a node to snapshot at the given dates; allocate the path matrix.
//! DateIndices are the exercise-grid columns; NbDraws is the number of MC paths
//! (matrix rows). Idempotent per node so several contracts sharing an underlying
//! record it once.
void NodeCollector::StartRecording( MonteCarloNode* Node,
                                    const vector<size_t>& DateIndices,
                                    size_t NbDraws )
{
    //! already recorded ? (e.g. two American contracts on the same underlying)
    for ( const auto& r : _records )
    {
        if ( r.node == Node )
        {
            return;
        }
    }
    PathRecord record;
    record.node = Node;
    record.date_index = DateIndices;
    //! precompute each column's year fraction from today — the LSM regression works
    //! in τ, so cache it now rather than reconverting dates on every path.
    for ( size_t idx : DateIndices )
    {
        record.tau.push_back( YearFraction( _date_list[0], _date_list[idx] ) );
    }
    //! [ nb_draws x nb_exercise_dates ] : one row per path, filled by RecordPath
    record.paths = la_matrix_alloc( NbDraws, DateIndices.size() );
    _records.push_back( std::move( record ) );
}

//! snapshot the current draw into the next row of each recorded matrix.
//! Called once per path after PriceNodes, while the node values for this draw are live.
void NodeCollector::RecordPath()
{
    for ( auto& r : _records )
    {
        //! defensive: never write past the pre-sized matrix if called extra times
        if ( r.row >= r.paths->size1 )
        {
            continue;
        }
        //! copy each recorded date's value into this draw's row
        for ( size_t c = 0; c < r.date_index.size(); c++ )
        {
            la_matrix_set( r.paths, r.row, c, r.node->GetValue( r.date_index[c] ) );
        }
        r.row++; //!< advance to the next draw's row
    }
}

//! recorded [ nb_draws x nb_exercise_dates ] matrix for a node, or nullptr.
//! Looked up by name (the LSM pass holds names, not node pointers); linear scan is
//! fine since _records holds only the handful of recorded underlyings.
const la_matrix* NodeCollector::RecordedPaths( const string& NodeName ) const
{
    for ( const auto& r : _records )
    {
        if ( r.node->GetName() == NodeName )
        {
            return r.paths.get();
        }
    }
    return nullptr;
}

//! year fractions of the recorded columns for a node (the τ grid for the LSM
//! regression); empty vector if the node was not recorded.
vector<double> NodeCollector::RecordedTau( const string& NodeName ) const
{
    for ( const auto& r : _records )
    {
        if ( r.node->GetName() == NodeName )
        {
            return r.tau;
        }
    }
    return {};
}

//! diffusion-date indices up to (and including) a maturity — the exercise grid.
//! _date_list is ascending, so this is every column an American option could be
//! exercised on for a contract maturing at Maturity.
vector<size_t> NodeCollector::DiffusionIndicesUpTo( const date& Maturity ) const
{
    vector<size_t> indices;
    for ( size_t i = 0; i < _date_list.size(); i++ )
    {
        if ( _date_list[i] <= Maturity )
        {
            indices.push_back( i );
        }
    }
    return indices;
}

//! evaluate the graph for the current path: walk the topologically sorted
//! (node, date-index) schedule produced by SortNodes, so every dependency is
//! already computed before the node that reads it.
void NodeCollector::PriceNodes()
{
    const size_t n = _node_list.size();
    for ( size_t i = 0;
          i < n;
          i++ )
    {
        MonteCarloNode* N = _node_list[i];
        size_t j = _date_index_list[i]; //!< the date index this entry evaluates at
        N->ComputeValue( j );           //!< the node's value for this draw at date j
        N->UpdateIndicators( j );       //!< update running stats (barrier hits, max/min, ...)
    }
}

//! debug : the node graph reachable from Root as Graphviz dot text. One node per
//! reachable node, one edge per (parent -> child) dependency (deduped across
//! diffusion dates; self dependencies omitted). Returned as a string so the pricer
//! can put it in the result block, travelling back to the client over HTTP / batch.
string NodeCollector::GraphDot( MonteCarloNode* Root ) const
{
    //! collect the nodes reachable from Root (the tree priced from that root),
    //! then index name -> child names so the emitted graph is deterministic.
    set<MonteCarloNode*> seen;            //!< visited set (raw pointer identity)
    vector<MonteCarloNode*> todo{ Root }; //!< DFS frontier, seeded with the root
    map<string, set<string>> edges;       //!< node name -> child names
    while ( !todo.empty() )
    {
        MonteCarloNode* n = todo.back();
        todo.pop_back();
        if ( n == nullptr || !seen.insert( n ).second )
        {
            continue; //!< null or already expanded -> skip
        }
        set<string>& children = edges[n->GetName()]; //!< ensures isolated nodes still emit
        //! a child can appear at several diffusion dates; union over all dates and
        //! dedup by name (set) so the drawing has one edge per distinct dependency.
        for ( size_t d = 0; d < _date_list.size(); d++ )
        {
            for ( const node& c : n->GetChildNodes( d ) )
            {
                if ( c.first != n ) //!< skip the diffusion self-dependency
                {
                    children.insert( c.first->GetName() );
                    todo.push_back( c.first );
                }
            }
        }
    }

    //! emit deterministic dot: maps/sets above iterate in name order, so identical
    //! graphs produce byte-identical output (stable across runs / platforms).
    std::ostringstream f;
    f << "digraph nodes {\n";
    f << "  rankdir=LR;\n";
    f << "  node [shape=box, fontsize=10];\n";
    for ( const auto& [name, children] : edges )
    {
        f << "  \"" << name << "\";\n";
        for ( const string& c : children )
        {
            f << "  \"" << name << "\" -> \"" << c << "\";\n";
        }
    }
    f << "}\n";
    return f.str();
}

//! single-root convenience overload — defer to the multi-root version.
void NodeCollector::SortNodes( MonteCarloNode& RootNode )
{
    SortNodes( vector<MonteCarloNode*>{ &RootNode } );
}

//! topologically sort the DAG reachable from Roots into the (_node_list,
//! _date_index_list) evaluation schedule consumed by PriceNodes. A graph "node" is
//! a (MonteCarloNode*, date-index) pair, so the same node at different dates is
//! scheduled independently. Implemented as Kahn's algorithm working from the
//! leaves up: nodes with no children are ready first, parents follow once all their
//! children are scheduled. Ordered-by-name structures keep the result reproducible.
void NodeCollector::SortNodes( const vector<MonteCarloNode*>& Roots )
{

    //! structures ( ordered by node name, not pointer, for reproducibility )
    map<node, node_set, NodeNameLess> node_links; // child -> ( father1 ,... )
    stack<node> nochild_nodes;                    //!< leaves ready to be emitted
    map<node, int, NodeNameLess> child_count;     //!< node -> # unscheduled children

    //! exploring tree to init algo (seed the DFS with every root; the visited
    //! set dedups nodes shared by several roots so each is scheduled once)
    stack<node> node_stack;
    node_set visited_set;
    for ( MonteCarloNode* r : Roots )
    {
        node_stack.push( node( r, 0 ) ); //!< root needed at its terminal date (index 0 slot)
    }

    //! recorded nodes (American LSM): force each recorded node — and hence its
    //! dependencies — to be evaluated at every recorded date. A diffusion spot is
    //! already scheduled at all dates via its self-dependency, but a derived spot
    //! (composite / basket) is otherwise only pulled in where the contract flow
    //! references it (maturity), leaving the interior path columns uncomputed.
    for ( const auto& rec : _records )
    {
        for ( size_t idx : rec.date_index )
        {
            node_stack.push( node( rec.node, idx ) );
        }
    }
    //! DFS over the dependency graph, building the parent links and child counts
    //! Kahn's algorithm needs. Each (node, date) is expanded at most once.
    while ( !node_stack.empty() )
    {
        node n = node_stack.top();
        node_stack.pop();
        if ( visited_set.find( n ) == visited_set.end() )
        {
            node_set s = n.first->GetChildNodes( n.second ); //!< this entry's dependencies
            if ( s.size() )
            {
                node_set::iterator i;
                for ( i = s.begin();
                      i != s.end();
                      i++ )
                {
                    node_stack.push( *i );      //!< explore the child
                    child_count[n] += 1;        //!< n waits on one more child
                    node_links[*i].insert( n ); //!< reverse edge child -> parent n
                }
            }
            else
            {
                nochild_nodes.push( n ); //!< a leaf: ready to emit immediately
            }

            visited_set.insert( n );
        }
    }

    //! topological sorting — drain the ready set (leaves first). Emitting a node
    //! "releases" its parents: each parent loses one outstanding child, and once a
    //! parent's count hits zero it becomes ready in turn. The resulting order has
    //! every child before its parent, exactly what PriceNodes requires.
    while ( !nochild_nodes.empty() )
    {

        //! top of the stack
        node n = nochild_nodes.top();
        nochild_nodes.pop();

        //! skip constants: they need no per-path evaluation, so keep them out of the
        //! schedule (their value is read directly) — pure size/perf optimisation.
        if ( !n.first->IsConstant( n.second ) )
        {
            _node_list.push_back( n.first );
            _date_index_list.push_back( n.second );
        }

        //! does it have links with parents ? release each parent that depended on n.
        map<node, node_set, NodeNameLess>::iterator i;
        while ( ( i = node_links.find( n ) ) != node_links.end() )
        {

            //! for each parent
            node_set::iterator j;
            for ( j = i->second.begin();
                  j != i->second.end();
                  j++ )
            {
                //! decrease nb of children
                map<node, int, NodeNameLess>::iterator k = child_count.find( *j );
                if ( k != child_count.end() )
                {
                    if ( k->second > 1 )
                    {
                        k->second -= 1; //!< still waiting on other children
                    }
                    //! if no more child -> add to nochild_nodes (parent now ready)
                    else
                    {
                        nochild_nodes.push( *j );
                        child_count.erase( k ); //!< done with this parent's counter
                    }
                }
            }

            //! remove link — n's reverse edges are consumed; erasing also terminates
            //! the while-find loop for this n.
            node_links.erase( i );
        }
    }
}

//! date -> diffusion-date index (reverse of _date_list). Errors on an unknown date,
//! since callers index _date_list / per-date arrays with the result.
size_t NodeCollector::GetDateIndex( const date& AsOfDate )
{
    auto it = _index_date_map.find( AsOfDate );
    if ( it == _index_date_map.end() )
    {
        ERR( "date " + to_simple_string( AsOfDate ) + " is not a diffusion date" );
    }
    return it->second;
}

//! number of owned nodes — used for logging / diagnostics (graph size).
size_t NodeCollector::GetNodeNumber()
{
    return _node_map.size();
}