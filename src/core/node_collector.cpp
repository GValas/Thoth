#include "thoth.hpp"
#include "node_collector.hpp"
#include <fstream>

//! constructor
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

//! push back
void NodeCollector::PushBrownianNode( std::unique_ptr<BrownianNode> N )
{
    _brownian_node_map[N->GetName()] = N.get(); //!< non-owning view
    PushNode( std::move( N ) );
}

//! push back
void NodeCollector::PushNode( std::unique_ptr<MonteCarloNode> N )
{
    N->SetDateList( _date_list );
    _node_map[N->GetName()] = std::move( N ); //!< adopt ownership
}

//! setter
void NodeCollector::SetDiffusionDates( const set<date>& DiffusionDates )
{
    _diffusion_dates = DiffusionDates;
    _today = *DiffusionDates.begin();

    //! init _previous_diffusion_dates
    set<date>::iterator d;
    date previous_date;
    size_t i = 0;
    for ( d = _diffusion_dates.begin();
          d != _diffusion_dates.end();
          d++ )
    {
        _date_list.push_back( *d );
        _index_date_map[*d] = i++;

        if ( *d != _today )
        {
            _previous_diffusion_dates[*d] = previous_date;
        }
        previous_date = *d;
    }
}

//! brownian is special : also registered in the non-owning _brownian_node_map
BrownianNode* NodeCollector::NewBrownianNode( const string& Name )
{
    auto n = std::make_unique<BrownianNode>( Name + _scenario_suffix );
    BrownianNode* p = n.get();
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

//! get previous diffusion date (the date must be a known diffusion date)
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

//! register a node to snapshot at the given dates; allocate the path matrix
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
    for ( size_t idx : DateIndices )
    {
        record.tau.push_back( YearFraction( _date_list[0], _date_list[idx] ) );
    }
    record.paths = gsl_matrix_alloc( NbDraws, DateIndices.size() );
    _records.push_back( std::move( record ) );
}

//! snapshot the current draw into the next row of each recorded matrix
void NodeCollector::RecordPath()
{
    for ( auto& r : _records )
    {
        if ( r.row >= r.paths->size1 )
        {
            continue;
        }
        for ( size_t c = 0; c < r.date_index.size(); c++ )
        {
            gsl_matrix_set( r.paths, r.row, c, r.node->GetValue( r.date_index[c] ) );
        }
        r.row++;
    }
}

//! recorded [ nb_draws x nb_exercise_dates ] matrix for a node, or nullptr
const gsl_matrix* NodeCollector::RecordedPaths( const string& NodeName ) const
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

//! year fractions of the recorded columns for a node
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

//! diffusion-date indices up to (and including) a maturity — the exercise grid
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

void NodeCollector::PriceNodes()
{
    const size_t n = _node_list.size();
    for ( size_t i = 0;
          i < n;
          i++ )
    {
        MonteCarloNode* N = _node_list[i];
        size_t j = _date_index_list[i];
        N->ComputeValue( j );
        N->UpdateIndicators( j );
        // double x = N->GetValue( j );
        // cout << N->GetName() << "@" << to_simple_string( _date_list[j] )  << " = " << x << endl;
    }
    // cout << " ------------------------- " << endl;
}

//! debug : write the node graph as Graphviz dot. One node per built node, one
//! edge per (parent -> child) dependency (deduped across diffusion dates; self
//! dependencies omitted).
void NodeCollector::ExportGraph( const string& Path ) const
{
    std::ofstream f( Path );
    if ( !f )
    {
        ERR( "debug: cannot open node-graph file '" + Path + "'" );
    }

    f << "digraph nodes {\n";
    f << "  rankdir=LR;\n";
    f << "  node [shape=box, fontsize=10];\n";

    for ( const auto& kv : _node_map )
    {
        MonteCarloNode* n = kv.second.get();
        f << "  \"" << n->GetName() << "\";\n";

        //! union of child node names over every diffusion date
        set<string> child_names;
        for ( size_t d = 0; d < _date_list.size(); d++ )
        {
            for ( const node& c : n->GetChildNodes( d ) )
            {
                if ( c.first != n ) //!< skip the diffusion self-dependency
                {
                    child_names.insert( c.first->GetName() );
                }
            }
        }
        for ( const string& c : child_names )
        {
            f << "  \"" << n->GetName() << "\" -> \"" << c << "\";\n";
        }
    }

    f << "}\n";
}

void NodeCollector::SortNodes( MonteCarloNode& RootNode )
{
    SortNodes( vector<MonteCarloNode*>{ &RootNode } );
}

void NodeCollector::SortNodes( const vector<MonteCarloNode*>& Roots )
{

    //! structures ( ordered by node name, not pointer, for reproducibility )
    map<node, node_set, NodeNameLess> node_links; // child -> ( father1 ,... )
    stack<node> nochild_nodes;
    map<node, int, NodeNameLess> child_count;

    //! exploring tree to init algo (seed the DFS with every root; the visited
    //! set dedups nodes shared by several roots so each is scheduled once)
    stack<node> node_stack;
    node_set visited_set;
    for ( MonteCarloNode* r : Roots )
    {
        node_stack.push( node( r, 0 ) );
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
    while ( !node_stack.empty() )
    {
        node n = node_stack.top();
        node_stack.pop();
        if ( visited_set.find( n ) == visited_set.end() )
        {
            node_set s = n.first->GetChildNodes( n.second );
            if ( s.size() )
            {
                node_set::iterator i;
                for ( i = s.begin();
                      i != s.end();
                      i++ )
                {
                    node_stack.push( *i );
                    child_count[n] += 1;
                    node_links[*i].insert( n );
                }
            }
            else
            {
                nochild_nodes.push( n );
            }

            visited_set.insert( n );
        }
    }

    //! topological sorting
    while ( !nochild_nodes.empty() )
    {

        //! top of the stack
        node n = nochild_nodes.top();
        nochild_nodes.pop();

        if ( !n.first->IsConstant( n.second ) )
        {
            _node_list.push_back( n.first );
            _date_index_list.push_back( n.second );
        }

        //! does it have links with parents ?
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
                        k->second -= 1;
                    }
                    //! if no more child -> add to nochild_nodes
                    else
                    {
                        nochild_nodes.push( *j );
                        child_count.erase( k );
                    }
                }
            }

            //! remove link
            node_links.erase( i );
        }
    }
}

size_t NodeCollector::GetDateIndex( const date& AsOfDate )
{
    auto it = _index_date_map.find( AsOfDate );
    if ( it == _index_date_map.end() )
    {
        ERR( "date " + to_simple_string( AsOfDate ) + " is not a diffusion date" );
    }
    return it->second;
}

size_t NodeCollector::GetNodeNumber()
{
    return _node_map.size();
}