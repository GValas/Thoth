#pragma once

#include "nodes.hpp"

#include <utility>

//! node_collector.hpp — the Monte-Carlo node graph owner and evaluator interface.

//! Owns and wires the Monte-Carlo node graph: builds/looks up nodes by name
//! (GetOrCreate, with per-Greek-scenario suffixing), holds the diffusion-date
//! schedule, topologically sorts the DAG (SortNodes) and evaluates it per path
//! (PriceNodes), and records per-path values for the American (Longstaff-Schwartz) pass.
class NodeCollector
{

  private:
    date _today;                               //!< valuation date = first diffusion date
    set<date> _diffusion_dates;                //!< the ascending schedule as provided
    vector<date> _date_list;                   //!< index -> date (the array nodes step over)
    map<date, date> _previous_diffusion_dates; //!< date -> immediately preceding date
    map<date, size_t> _index_date_map;         //!< date -> index (reverse of _date_list)

    map<string, std::unique_ptr<MonteCarloNode>> _node_map; //!< owns every node
    map<string, BrownianNode*> _brownian_node_map;          //!< non-owning view

    //! Greek-scenario build context (set via SetScenario, read via Scenario()).
    struct ScenarioContext
    {
        string suffix;           //!< node-name suffix while building a Greek bump ("" = base tree)
        bool bumps_rate = false; //!< the scenario bumps rates (rho)
        bool bumps_vol = false;  //!< the scenario bumps vols (vega)
        bool active() const { return !suffix.empty(); }
    };
    ScenarioContext _scenario;

    //! the evaluation schedule produced by SortNodes and consumed by PriceNodes:
    //! two parallel arrays forming a flat list of (node, date-index) work items in
    //! topological order (every dependency precedes the node that reads it).
    //! (node,index)
    //! indicator sub-schedule: the (node, date) entries of the sorted schedule
    //! whose node accumulates statistics (the book + the contracts + the scenario
    //! roots — a handful), so the hot loop skips the UpdateIndicators call/branch
    //! for the thousands of intermediate diffusion entries.
    vector<MonteCarloNode*> _indicator_node_list;
    vector<size_t> _indicator_date_index_list;

    vector<MonteCarloNode*> _node_list;
    vector<size_t> _date_index_list;

    //! (the per-path American recorder lives in PathRecorder now, owned by the pricer)

    //! ownership sinks shared by the public New*Node helpers: PushNode adopts any
    //! node into _node_map (and hands it the date schedule); PushBrownianNode also
    //! adds the non-owning Brownian index. Private so all creation goes through New*.
    void PushNode( std::unique_ptr<MonteCarloNode> N );
    void PushBrownianNode( std::unique_ptr<BrownianNode> N );

  public:
    //! allocate a node, adopt ownership, return the observer pointer. Replaces
    //! the former per-type New*Node forwarders (one template, no touchpoint).
    //! The current scenario suffix (empty by default) is appended to the node
    //! name so that a Greek-bump scenario builds its own copy of every
    //! spot-dependent node while sharing the un-suffixed Brownian/noise nodes.
    template <class T>
    T* NewNode( const string& Name )
    {
        auto n = std::make_unique<T>( Name + _scenario.suffix );
        T* p = n.get();
        PushNode( std::move( n ) );
        return p;
    }

    //! return the node named Name (in the current scenario) if it already
    //! exists, otherwise build a T, configure it via init(T*), register it, and
    //! return it. Collapses the "look up by name, else build" idiom.
    template <class T, class Init>
    MonteCarloNode* GetOrCreate( const string& Name, Init init )
    {
        if ( MonteCarloNode* existing = GetNode( Name + _scenario.suffix ) )
        {
            return existing;
        }
        T* node = NewNode<T>( Name ); //!< NewNode appends the scenario suffix
        init( node );
        return node;
    }

    //! like GetOrCreate but IGNORES the scenario suffix: the node is built/looked
    //! up under its bare name, so it is shared with the base tree. Used for the
    //! market-data leaves (rate / vol / drift) a Greek scenario does not bump, so
    //! they are mutualised instead of duplicated per scenario.
    template <class T, class Init>
    MonteCarloNode* GetOrCreateShared( const string& Name, Init init )
    {
        if ( MonteCarloNode* existing = GetNode( Name ) )
        {
            return existing;
        }
        string saved = _scenario.suffix;
        _scenario.suffix.clear(); //!< build under the bare name
        T* node = NewNode<T>( Name );
        _scenario.suffix = saved;
        init( node );
        return node;
    }

    //! Greek-scenario tagging: while a suffix is set, every node built through
    //! NewNode/GetOrCreate is named "<base><suffix>", so the bumped sub-tree is
    //! distinct from the base tree but reuses the shared Brownian/noise nodes
    //! (which are looked up by their bare name via GetNode). Empty = base tree.
    //! The bump flags let the market-data leaves stay shared (GetOrCreateShared)
    //! unless the scenario actually bumps them (vega -> vol, rho -> rate).
    void SetScenario( const string& Suffix, bool BumpsRate, bool BumpsVol )
    {
        _scenario = { Suffix, BumpsRate, BumpsVol };
    }
    void ClearScenario() { _scenario = {}; } //!< back to the base tree
    const ScenarioContext& Scenario() const { return _scenario; }

    //! RAII pairing of SetScenario/ClearScenario: an exception while building a
    //! Greek-bump sub-tree (e.g. an ERR inside a node factory) must not leave the
    //! collector stuck in scenario mode — every later node would silently be
    //! built suffixed into the wrong tree.
    struct ScenarioScope
    {
        NodeCollector& nc;
        ScenarioScope( NodeCollector& NC, const string& Suffix, bool BumpsRate, bool BumpsVol )
            : nc( NC )
        {
            nc.SetScenario( Suffix, BumpsRate, BumpsVol );
        }
        ~ScenarioScope() { nc.ClearScenario(); }
        ScenarioScope( const ScenarioScope& ) = delete;
        ScenarioScope& operator=( const ScenarioScope& ) = delete;
    };

    //! getter
    size_t GetDateIndex( const date& AsOfDate );

    //! the diffusion date schedule (ascending; [0] is today). Available after
    //! SetDiffusionDates, so node builders can precompute per-date data.
    const vector<date>& GetDateList() const { return _date_list; }

    //! drop every node, date map and recording so the owning pricer can rebuild
    //! a fresh tree (used by the bump-and-revalue Greeks: each scenario rebuilds
    //! the graph so the bumped market is picked up by the nodes)
    void Reset();

    //! setter
    void SetDiffusionDates( const set<date>& DiffusionDates );

    //! brownian is special: also indexed in the non-owning _brownian_node_map
    BrownianNode* NewBrownianNode( const string& Name );
    ProductNode* NewProductNode( const string& Name ); //!< build a product (payoff) node

    //! getter — by name, nullptr on miss (no insertion, so a typo cannot pollute the
    //! owning map with a null slot). _brownian variant returns the typed view.
    MonteCarloNode* GetNode( const string& Name );
    BrownianNode* GetBrownianNode( const string& Name );

    //! typed lookup: the node named Name as a T*, or nullptr if absent / another
    //! type. Confines the heterogeneous-graph downcast to one place instead of a
    //! raw dynamic_cast at each call site.
    template <class T>
    T* GetTypedNode( const string& Name )
    {
        return dynamic_cast<T*>( GetNode( Name ) );
    }

    //! log — number of owned nodes (graph size), for diagnostics
    size_t GetNodeNumber();

    //! the diffusion date immediately before AsOfDate (errors if AsOfDate is not a
    //! known diffusion date) — used by diffusion nodes to step from the prior date.
    date PreviousDiffusionDate( const date& AsOfDate );

    //! debug : the node graph reachable from Root (the nodes priced for one tree)
    //! as Graphviz .dot text. Self-edges (a diffusion node reading its own previous
    //! value) are omitted; edges are deduped across diffusion dates; nodes/edges are
    //! emitted in name order so the output is deterministic.
    string GraphDot( MonteCarloNode* Root ) const;

    //! pricing — build the evaluation schedule, then run it per path
    void SortNodes( MonteCarloNode& RootNode ); //!< single-root convenience overload
    //! topologically sort the union DAG reachable from several roots into one
    //! evaluation schedule (shared nodes appear once) — used so the base tree
    //! and every Greek-bump sub-tree are priced in a single path sweep. ForcedPoints
    //! are extra (node, date-index) work items the schedule must include even if no
    //! root reaches them there — the American recorder's columns (PathRecorder::
    //! SchedulePoints), so a derived spot's interior path is computed, not just maturity.
    void SortNodes( const vector<MonteCarloNode*>& Roots,
                    const vector<std::pair<MonteCarloNode*, size_t>>& ForcedPoints = {} );
    void PriceNodes(); //!< evaluate the sorted schedule for the current draw

    //! diffusion-date indices up to (and including) Maturity — the exercise grid an
    //! American/barrier contract steps over. A pure date-schedule query (the per-path
    //! recording itself lives in PathRecorder), used by the pricer and the barrier.
    vector<size_t> DiffusionIndicesUpTo( const date& Maturity ) const;

    //!
    //! constructor / destructor — the destructor frees every owned node via the
    //! unique_ptr in _node_map (the Brownian index is non-owning).
    NodeCollector();
    ~NodeCollector();
};
