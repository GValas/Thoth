#pragma once

#include "nodes.hpp"

//! Owns and wires the Monte-Carlo node graph: builds/looks up nodes by name
//! (GetOrCreate, with per-Greek-scenario suffixing), holds the diffusion-date
//! schedule, topologically sorts the DAG (SortNodes) and evaluates it per path
//! (PriceNodes), and records per-path values for the American (Longstaff-Schwartz) pass.
class NodeCollector
{

  private:
    date _today;
    set<date> _diffusion_dates;
    vector<date> _date_list;
    map<date, date> _previous_diffusion_dates;
    map<date, size_t> _index_date_map;

    map<string, std::unique_ptr<MonteCarloNode>> _node_map; //!< owns every node
    map<string, BrownianNode*> _brownian_node_map;          //!< non-owning view

    string _scenario_suffix;           //!< appended to node names while building a Greek bump
    bool _scenario_bumps_rate = false; //!< scenario bumps rates (rho)
    bool _scenario_bumps_vol = false;  //!< scenario bumps vols (vega)

    //! (node,index)
    vector<MonteCarloNode*> _node_list;
    vector<size_t> _date_index_list;

    //! per-path recording : snapshot a node's values at chosen dates, every draw
    //! ( opt-in; used to feed the American / Longstaff-Schwartz pricer )
    struct PathRecord
    {
        MonteCarloNode* node = nullptr;
        vector<size_t> date_index; //!< columns: diffusion-date indices
        vector<double> tau;        //!< year fraction of each column from today
        LaMatrix paths;            //!< [ nb_draws x date_index.size() ]
        size_t row = 0;            //!< next draw to fill
    };
    vector<PathRecord> _records;

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
        auto n = std::make_unique<T>( Name + _scenario_suffix );
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
        if ( MonteCarloNode* existing = GetNode( Name + _scenario_suffix ) )
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
        string saved = _scenario_suffix;
        _scenario_suffix.clear(); //!< build under the bare name
        T* node = NewNode<T>( Name );
        _scenario_suffix = saved;
        init( node );
        return node;
    }

    //! Greek-scenario tagging: while a suffix is set, every node built through
    //! NewNode/GetOrCreate is named "<base><suffix>", so the bumped sub-tree is
    //! distinct from the base tree but reuses the shared Brownian/noise nodes
    //! (which are looked up by their bare name via GetNode). Empty = base tree.
    //! The bump flags let the market-data leaves stay shared (GetOrCreateShared)
    //! unless the scenario actually bumps them (vega -> vol, rho -> rate).
    void SetScenarioSuffix( const string& Suffix ) { _scenario_suffix = Suffix; }
    const string& GetScenarioSuffix() const { return _scenario_suffix; }
    void SetScenarioBumps( bool Rate, bool Vol )
    {
        _scenario_bumps_rate = Rate;
        _scenario_bumps_vol = Vol;
    }
    bool HasScenario() const { return !_scenario_suffix.empty(); }
    bool ScenarioBumpsRate() const { return _scenario_bumps_rate; }
    bool ScenarioBumpsVol() const { return _scenario_bumps_vol; }

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
    ProductNode* NewProductNode( const string& Name );

    //! getter
    MonteCarloNode* GetNode( const string& Name );
    BrownianNode* GetBrownianNode( const string& Name );

    //! log
    size_t GetNodeNumber();

    //!
    date PreviousDiffusionDate( const date& AsOfDate );

    //! debug : dump the built node graph (every node and its child edges) to a
    //! Graphviz .dot file. Self-edges (a diffusion node reading its own previous
    //! value) are omitted. Edges are deduped across diffusion dates.
    void ExportGraph( const string& Path ) const;

    //! pricing
    void SortNodes( MonteCarloNode& RootNode );
    //! topologically sort the union DAG reachable from several roots into one
    //! evaluation schedule (shared nodes appear once) — used so the base tree
    //! and every Greek-bump sub-tree are priced in a single path sweep.
    void SortNodes( const vector<MonteCarloNode*>& Roots );
    void PriceNodes();

    //! path recording (American / path-dependent) — opt-in, no effect when unused
    void StartRecording( MonteCarloNode* Node,
                         const vector<size_t>& DateIndices,
                         size_t NbDraws );
    void RecordPath(); //!< call once per draw, after PriceNodes
    bool IsRecording() const { return !_records.empty(); }
    const la_matrix* RecordedPaths( const string& NodeName ) const;
    vector<double> RecordedTau( const string& NodeName ) const;
    vector<size_t> DiffusionIndicesUpTo( const date& Maturity ) const;

    //!
    NodeCollector();
    ~NodeCollector();
};
