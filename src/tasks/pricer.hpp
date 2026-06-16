#pragma once
#include "book.hpp"
#include "correlation.hpp"
#include "debug_configuration.hpp"
#include "pricer_configuration.hpp"
#include "single.hpp"
#include "task.hpp"

//! constants
//! grid sizes per Precision level (see PricerConfiguration.h)
const int PDE_VANILLA_PRECISION_LOW_N_S = 501;
const int PDE_VANILLA_PRECISION_LOW_N_T = 301;
const int PDE_VANILLA_PRECISION_MEDIUM_N_S = 1001;
const int PDE_VANILLA_PRECISION_MEDIUM_N_T = 601;
const int PDE_VANILLA_PRECISION_HIGH_N_S = 1501;
const int PDE_VANILLA_PRECISION_HIGH_N_T = 1301;
const double PDE_SIGMA_FACTOR = 5.0;
const double PDE_THETA = 0.5;
const bool MC_USE_SOBOL = true;

//! pricing method selectors (config field "method")
inline constexpr char PRICING_METHOD_PDE[] = "pde";         //!< finite-difference PDE grid solving
inline constexpr char PRICING_METHOD_MCL[] = "mcl";         //!< monte-carlo (longstaff-schwartz) tree
inline constexpr char PRICING_METHOD_ANA[] = "ana";         //!< closed-form (analytic) formulas
inline constexpr char PRICING_METHOD_MCL_GPU[] = "mcl_gpu"; //!< GPU (CUDA) monte-carlo; CPU MCL fallback

inline constexpr char MCL_NODE_PATH[] = "";

//! finite-difference bump sizes for the bump-and-revalue Greeks
const double GREEK_SPOT_BUMP = 0.01;   //!< relative spot bump (1%) for delta
const double GREEK_GAMMA_BUMP = 0.10;  //!< wider relative spot bump (10%) for gamma:
                                       //!< a small second difference is swamped by the
                                       //!< PDE grid's per-spot re-centering noise, so
                                       //!< gamma needs the curvature signal to dominate
const double GREEK_VOL_BUMP = 0.01;    //!< absolute vol bump (1 vol point) for vega
const double GREEK_RATE_BUMP = 0.0001; //!< absolute rate bump (1 bp) for rho

class Pricer : public Task
{

  protected:
    //! attributes
    Book* _book = nullptr;
    Correlation* _correlation = nullptr;
    PricerConfiguration* _configuration = nullptr;
    DebugConfiguration* _debug = nullptr; //!< optional debug switches (null = off)
    vector<string> _indicator_request_list;
    Currency* _currency = nullptr;

    SingleSet _single_set;
    CurrencySet _currency_set;

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

    //! computed book-level results (book currency)
    double _premium = 0;
    double _delta = 0; //!< dPremium / dSpot
    double _gamma = 0; //!< d2Premium / dSpot2
    double _vega = 0;  //!< premium change per 1 vol point (0.01 of vol)
    double _rho = 0;   //!< premium change per 1% (0.01) parallel rate move
    double _theta = 0; //!< premium change over one calendar day (usually < 0)

    //! dates & underlyings
    set<date> GetFixingDates();
    vector<double> GetCorrelationMatrix();

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

    //! shared contract loop for the per-contract engines: prices each contract,
    //! computes its Greeks (when requested) by bumping only that contract's
    //! market, and advances a single progress bar over the contracts.
    void PriceBookByContract( const string& Label );

    //! bump-and-revalue Greeks for one contract (delta, gamma, vega, rho, theta),
    //! repricing only that contract; restores its base scenario when done.
    void ComputeContractGreeks( Contract* Ctr );

    //! bump-and-revalue Greeks for the indicators requested (delta, gamma, vega,
    //! rho, theta); leaves the book back at the base scenario when done. Virtual
    //! so MCL can override it with a single-tree (shared-path) implementation.
    virtual void ComputeGreeks();
    void ApplyVolShift( double Shift );  //!< parallel vol shift on every underlying
    void ApplyRateShift( double Shift ); //!< parallel shift on every currency's curve

    //! add a priced contract's premium/delta/gamma to the book (FX-converted to
    //! the book currency). Shared by every engine's Execute().
    void AggregateContract( Contract* Ctr );

    //! FX factor converting a contract premium into the book currency (1 when
    //! the currencies match). Shared by AggregateContract and the Greek sums.
    double FxToBook( Contract* Ctr );

    //! verify every contract in the book supports the engine's pricing method
    void CheckAllowed( const std::function<bool( Contract* )>& HasSolution,
                       const string& MethodLabel );

  public:
    // setter
    void SetBook( Book& Book );
    void SetCorrelation( Correlation* Correlation );
    void SetConfiguration( PricerConfiguration& Configuration );
    void SetDebugConfiguration( DebugConfiguration* Debug ) { _debug = Debug; }
    void SetIndicatorRequestList( const vector<string>& IndicatorRequestList );
    void SetCurrency( Currency& Currency );

    //! getter
    date GetToday() const;

    //! constructor & destructor
    Pricer( const string& ObjectName,
            YamlConfig& YamlConfig );
    ~Pricer() override;

    //!
    void Execute() override;
    void WriteResults() override;
};
