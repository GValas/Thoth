#pragma once
#include "book.hpp"
#include "correlation.hpp"
#include "debug_configuration.hpp"
#include "single.hpp"
#include "task.hpp"
#include "valuation.hpp"
#include <functional>
#include <map>

//! constants
//! Crank-Nicolson time-weighting of the PDE solver (a solver parameter, not a
//! configured field). The configured grid-size / sigma defaults live next to the
//! object that reads them (pde_configuration.hpp); the MCL defaults likewise.
inline constexpr double PDE_THETA = 0.5;

//! finite-difference bump sizes for the bump-and-revalue Greeks. The spot bump
//! (GREEK_SPOT_BUMP, for delta/gamma) is the canonical one in constants.hpp, so
//! the contracts share it; the rest are pricer-local.
inline constexpr double GREEK_GAMMA_BUMP = 0.10;  //!< wider relative spot bump (10%) for gamma:
                                                  //!< a small second difference is swamped by the
                                                  //!< PDE grid's per-spot re-centering noise, so
                                                  //!< gamma needs the curvature signal to dominate
inline constexpr double GREEK_VOL_BUMP = 0.01;    //!< absolute vol bump (1 vol point) for vega
inline constexpr double GREEK_RATE_BUMP = 0.0001; //!< absolute rate bump (1 bp) for rho
inline constexpr double GREEK_PARAM_BUMP = 0.01;  //!< absolute bump on a model parameter for the
                                                  //!< vega_<param> Greeks (SABR / Heston / Bates),
                                                  //!< reported per unit parameter. Sized like the vega
                                                  //!< bump (one point) so the signal clears the MC /
                                                  //!< PDE-grid / local-vol-resampling noise floor

class Pricer : public Task
{

  protected:
    //! attributes
    Book* _book = nullptr;
    Correlation* _correlation = nullptr;
    DebugConfiguration* _debug = nullptr; //!< optional debug switches (null = off)
    vector<string> _indicator_request_list;
    Currency* _currency = nullptr;

    //! engine's short tag for the per-contract progress bar ("ANA"/"PDE"/"GPU"); a
    //! per-class constant fixed by each subclass through the constructor.
    string _progress_label;

    //! engine-specific result fields, written from the common WriteResults after the
    //! premium. Default no-op; the MCL engine overrides it to emit its node-graph
    //! .dot fields (<tree>_mcl_graph). Keeps the MCL-only graph state out of this base.
    virtual void WriteEngineResults() {}

    //! tag the node graph captured by the next bump-and-revalue reprice. The base
    //! greek engine (BumpAndRevalueGreeks) calls this around each bump with the tree
    //! name ("delta"/"gamma"/"vega"/"rho"/"theta"), or "" to clear; only the MCL
    //! engine acts on it (to key that reprice's graph), so ANA/PDE use this no-op.
    virtual void TagRepriceGraph( const string& /*Tag*/ ) {}

    SingleSet _single_set;
    CurrencySet _currency_set;
    ContractSet _contract_set;

    //! set while the book-level ComputeGreeks (MCL) re-prices the whole book for
    //! bumped scenarios, so the engine silences its progress bar for those inner
    //! prices (the bar is shown only for the one base price, not per Greek bump).
    //! The per-contract engines (PDE, ANA) instead show one bar over the contract
    //! loop that already covers each contract's Greeks, so they never set this.
    bool _quiet_pricing = false;

    //! requested indicators
    bool _request_premium = false;
    bool _request_delta = false;
    bool _request_gamma = false;
    bool _request_vega = false;
    bool _request_rho = false;
    bool _request_theta = false;

    //! requested model-parameter Greeks: each "vega_<param>" indicator adds <param>
    //! here (alpha, kappa, jump_vol, ...). Computed book-level by bumping that model
    //! parameter on every underlying whose vol surface exposes it.
    vector<string> _param_greek_list;

    //! computed results (book currency). The financial objects (Contract / Book)
    //! are pure descriptions — all priced state lives here on the task: the
    //! per-contract Valuations keyed by contract name, plus the aggregated book
    //! Valuation. The map is pre-populated in InitPricing, so Result().at() always
    //! resolves; the engines write premium / Greeks through Result(Ctr).
    map<string, Valuation> _contract_results;
    Valuation _book_result;

    //! per-contract priced result, keyed by contract name (replaces the old
    //! Contract::Result()). Hard-fails on a missing key (an engine bug), which is
    //! why InitPricing pre-populates an entry for every contract in the book.
    Valuation& Result( Contract* Ctr ) { return _contract_results.at( Ctr->GetName() ); }
    const Valuation& Result( Contract* Ctr ) const { return _contract_results.at( Ctr->GetName() ); }

    //! vega_<param> results, keyed by parameter name (book currency, per unit param)
    map<string, double> _param_greeks;

    //! dates & underlyings
    set<date> GetFixingDates();

    //! init objects before pricing
    void InitPricing();

    //! engine-specific hooks (one orchestration in Pricer::Execute drives them):
    //! PreCheck validates the book/config once; PriceBook prices the whole book
    //! for the current market state into the book accumulators (re-runnable, so
    //! the bump-and-revalue Greeks can call it repeatedly with a bumped market).
    virtual void PreCheck() {}
    virtual void PriceBook() = 0;

    //! price a single contract for the current market state (engines that can
    //! isolate one contract override this: PDE solves its grid, ANA its formula).
    //! Used by the per-contract loop and its bump-and-revalue Greeks.
    virtual void PriceContract( Contract* Ctr );

    //! true for engines whose Greeks are computed per contract inside the
    //! contract loop (PDE, ANA). MCL stays book-level (correlated diffusion can
    //! not isolate a single contract), so it keeps ComputeGreeks.
    virtual bool GreeksPerContract() const { return false; }

    //! true for an engine whose PriceContract already yields spot delta/gamma from
    //! its grid (PDE). For a multi-asset underlying, whose basket "spot" is fixed
    //! (a rebased 100, independent of the component spots), a per-component spot
    //! bump can't move it, so we keep the grid's delta/gamma instead of the bump.
    virtual bool GridSpotGreeks() const { return false; }

    //! shared contract loop for the per-contract engines: prices each contract,
    //! computes its Greeks (when requested) by bumping only that contract's market,
    //! and advances a single progress bar (labelled by _progress_label) over them.
    void PriceBookByContract();

    //! delta/gamma/vega/rho/theta produced by one bump-and-revalue sweep
    struct BumpGreeks
    {
        double delta = 0;
        double gamma = 0;
        double vega = 0;
        double rho = 0;
        double theta = 0;
    };

    //! shared finite-difference bump-and-revalue engine behind both the book-level
    //! ComputeGreeks and the per-contract ComputeContractGreeks. For each requested
    //! Greek it bumps the market input (spot per underlying for delta/gamma, a
    //! parallel vol bump on Singles for vega, a parallel rate bump on Currencies
    //! for rho, a one-day roll for theta), calls Reprice() — which reprices the
    //! current market and returns the premium to difference against P0 — restores
    //! the input, and calls Tick() for the progress bar. RollToday sets the
    //! valuation date (the book caller moves only _today; the contract caller also
    //! rolls the contract). Market inputs are left at base on return; the premium
    //! still reflects the last (theta) bump, so the caller does its own final
    //! restore reprice before reading results.
    BumpGreeks BumpAndRevalueGreeks( double P0,
                                     const vector<Single*>& Singles,
                                     const CurrencySet& Currencies,
                                     bool DoDelta,
                                     bool DoGamma,
                                     bool DoVega,
                                     bool DoRho,
                                     bool DoTheta,
                                     const std::function<double()>& Reprice,
                                     const std::function<void( const date& )>& RollToday,
                                     const std::function<void()>& Tick );

    //! bump-and-revalue Greeks for one contract (delta, gamma, vega, rho, theta),
    //! repricing only that contract; restores its base scenario when done.
    void ComputeContractGreeks( Contract* Ctr );

    //! bump-and-revalue Greeks for the indicators requested (delta, gamma, vega,
    //! rho, theta); leaves the book back at the base scenario when done. Virtual
    //! so MCL can override it with a single-tree (shared-path) implementation.
    virtual void ComputeGreeks();
    void ApplyVolShift( double Shift );  //!< parallel vol shift on every underlying
    void ApplyRateShift( double Shift ); //!< parallel shift on every currency's curve

    //! model-parameter Greeks (vega_<param>): book-level bump-and-revalue of each
    //! requested parameter, shared by every engine (called from Execute after the
    //! standard Greeks). Skips a parameter no underlying's vol surface exposes.
    void ComputeParamGreeks();

    //! add a priced contract's premium/delta/gamma to the book (FX-converted to
    //! the book currency). Shared by every engine's Execute().
    void AggregateContract( Contract* Ctr );

    //! FX factor converting a contract premium into the book currency (1 when
    //! the currencies match). Shared by AggregateContract and the Greek sums.
    double FxToBook( Contract* Ctr );

    //! verify every contract in the book supports the engine's pricing method
    void CheckAllowed( const std::function<bool( Contract* )>& HasSolution,
                       const string& MethodLabel );

    //! every engine writes the same "pricer_result" block kind (the three pricer
    //! kinds share one result schema), rather than the per-kind default.
    string ResultKind() const override { return "pricer_result"; }

  public:
    //! read the fields common to every pricer (currency / book / today /
    //! configuration / indicators / result, with optional correlation & debug).
    //! The concrete pricer class is chosen by the registry before this runs.
    void Configure( ObjectReader& reader ) override;

    // setter
    void SetBook( Book* Book );
    void SetCorrelation( Correlation* Correlation );
    void SetDebugConfiguration( DebugConfiguration* Debug ) { _debug = Debug; }
    void SetIndicatorRequestList( const vector<string>& IndicatorRequestList );
    void SetCurrency( Currency* Currency );

    //! getter
    date GetToday() const;

    //! constructor & destructor. ObjectKind is the concrete engine kind and
    //! ProgressLabel its short per-contract-bar tag — both passed up by the
    //! PricerMCL / PricerPDE / PricerANA subclass (the registry picks the subclass
    //! straight off the YAML tag, so each one knows its own kind / label).
    Pricer( const string& ObjectName,
            YamlConfig& YamlConfig,
            const string& ObjectKind,
            const string& ProgressLabel );
    ~Pricer() override;

    //!
    void Execute() override;
    void WriteResults() override;
};
