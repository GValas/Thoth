#pragma once
#include "mcl_configuration.hpp"
#include "pricer.hpp"
#include "rng.hpp"
#include <memory>

class PathGenerator; //!< Sobol + Brownian-bridge per-path Gaussian generator
class ProgressBar;   //!< shared between the path sweep and the American LSM pass

//! The Monte-Carlo engine: builds the diffusion-date schedule and node graph, draws
//! paths (pseudo-random or Sobol + Brownian bridge), prices the book (with shared
//! single-tree Greeks), and runs the Longstaff-Schwartz post-pass for American
//! exercise (the "AMC" leg).
class PricerMCL : public Pricer
{

  private:
    //! Monte-Carlo engine parameters (grid steps, path count, Sobol/GPU switches),
    //! referenced directly from the !mcl_pricer node via its "mcl_configuration"
    //! field and resolved in Configure. Borrowed (owned by the ObjectManager), so it
    //! may be shared by several pricers.
    MclConfiguration* _mcl = nullptr;

    //! random numbers
    Rng _rng;

    //! one progress bar spanning the WHOLE American job: the path sweep fills the
    //! first part, the Longstaff-Schwartz post-pass (PriceAmerican / fit) fills
    //! the rest, so the bar only reaches 100% once the American premium is ready.
    //! For non-American books it covers just the sweep.
    long _progress_step = 0; //!< running position shared by the sweep and the LSM fit

    //! optional quasi-random path generator (Sobol + Brownian bridge); only
    //! created when the configuration sets use_sobol
    std::unique_ptr<PathGenerator> _path_generator;
    void SetupQuasiRandom();

    //! dates
    set<date> _diffusion_dates;
    void InitDates();

    // mcl nodes
    NodeCollector _collector;
    MonteCarloNode* _root;

    //! single-tree Greeks: the base tree and every spot/vol/rate bump sub-tree
    //! are built into one collector and priced in a single path sweep (sharing
    //! the Brownian/noise nodes). _scenario_roots maps a bump tag to its book
    //! node; _scenario_premium holds each bump's MC premium after Tree_Read.
    bool _build_greek_scenarios = false; //!< set per PriceBook: build the bump sub-trees
    bool _suppress_scenarios = false;    //!< force a base-only tree (theta reprice / fallback)
    bool _theta_pass = false;            //!< the theta one-day reprice: show a local "MCL theta"
                                         //!< bar even while quiet, but keep it out of GlobalProgress
    vector<std::pair<string, MonteCarloNode*>> _scenario_roots;
    map<string, double> _scenario_premium;
    bool CanSingleTreeGreeks() const; //!< true iff every underlying is Mono (American is handled by a frozen LSM policy)
    void BuildGreekScenarios();       //!< build the delta/gamma/vega/rho bump sub-trees

    //! GPU (CUDA) acceleration, opt-in via the mcl_configuration's allow_gpu flag.
    //! Decided once in PreCheck: true iff allow_gpu AND a device is present AND the
    //! whole book is GPU-supported (single-asset European vanillas under GBM). When
    //! false the engine runs entirely on the CPU (the diffusion-tree path below).
    bool _use_gpu = false;
    bool BookIsGpuSupported(); //!< gpu::Available() && every contract GPU_GbmParams

    //! tree
    void Tree_Init();
    void Tree_Run();
    void Tree_Read();

    void CreateBrownianNodes();
    void CorrelateBrownianNodes();
    void CreateContractualNodes();

    void ComputeCholeskyMatrix();

    //! American path recording (opt-in : only when American contracts exist)
    void SetupAmericanRecording();
    void LogRecordings();
    long AmericanLsmSteps() const; //!< progress-bar steps the LSM fit will run
    string LogLabel() const;       //!< "AMC" for an American (LSM) run, else "MCL"
    //! the diffusion node carrying the contract's exercise value. Resolved from
    //! the underlying itself (not the "<name>#spot" convention) so it also works
    //! for composite / basket, whose spot node is named differently from the
    //! underlying. Empty if the node has not been built.
    string AmericanSpotName( Contract* Contract );

    //! American pricing via Longstaff-Schwartz on the recorded paths.
    //!
    //! Single-tree Greeks for American options: the exercise policy is fit ONCE
    //! on the base paths (FitAmericanPolicy) and then applied as a FROZEN rule to
    //! the base paths and to every bumped scenario's recorded paths
    //! (ApplyAmericanPolicy). Freezing the boundary across bumps is both cheaper
    //! (no re-regression per bump) and lower-variance/smoother (the envelope
    //! theorem makes a first-order boundary error contribute only at second order).
    struct AmericanPolicy
    {
        double basis_norm = 0; //!< moneyness normaliser m = S/basis_norm (the strike)
        vector<double> tau;    //!< year fractions of the recorded exercise grid
        vector<double> b0;     //!< continuation regression constant per exercise date
        vector<double> b1;     //!< continuation regression slope (m) per exercise date
        vector<double> b2;     //!< continuation regression curvature (m^2) per exercise date
        vector<bool> has_fit;  //!< true where a continuation fit exists (enough ITM points)
    };
    void PriceAmerican();
    AmericanPolicy FitAmericanPolicy( Contract* Contract,
                                      const la_matrix* Paths,
                                      const vector<double>& Tau,
                                      double Rate );
    double ApplyAmericanPolicy( Contract* Contract,
                                const la_matrix* Paths,
                                const vector<double>& Tau,
                                double Rate,
                                const AmericanPolicy& Policy,
                                double& Trust );

  protected:
    void PreCheck() override;  //!< require correlation; decide GPU vs CPU
    void PriceBook() override; //!< CPU diffusion tree, or the GPU per-contract loop when _use_gpu
    //! single-tree Greeks (delta/gamma/vega/rho in one path sweep); theta stays
    //! bump-and-revalue. Falls back to Pricer::ComputeGreeks when !CanSingleTreeGreeks.
    void ComputeGreeks() override;
    //! GPU mode prices contract by contract (per-contract bump Greeks, common
    //! random numbers); the CPU path keeps MCL's book-level single-tree Greeks.
    bool GreeksPerContract() const override;      //!< true iff _use_gpu
    void PriceContract( Contract* Ctr ) override; //!< one contract on the device (GPU mode)

  public:
    //! read the common pricer fields (Pricer::Configure) then resolve the required
    //! "mcl_configuration" reference for this engine's parameters. Public (like the
    //! base) so the registry's factory can configure the freshly built object.
    void Configure( ObjectReader& reader ) override;

    //! constructor, destructor
    PricerMCL( const string& ObjectName,
               YamlConfig& YamlConfig );
    ~PricerMCL() override;
};
