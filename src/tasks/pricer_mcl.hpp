#pragma once
#include "pricer.hpp"
#include <memory>

class PathGenerator; //!< Sobol + Brownian-bridge per-path Gaussian generator

class PricerMCL : public Pricer
{

  private:
    //! random numbers
    GslRng _gsl_r;

    //! optional quasi-random path generator (Sobol + Brownian bridge); only
    //! created when the configuration sets use_sobol
    std::unique_ptr<PathGenerator> _path_generator;
    void SetupQuasiRandom_();

    //! dates
    set<date> _diffusion_dates;
    void InitDates();

    // mcl nodes
    NodeCollector _collector;
    MonteCarloNode* _root;

    //! single-tree Greeks: the base tree and every spot/vol/rate bump sub-tree
    //! are built into one collector and priced in a single path sweep (sharing
    //! the Brownian/noise nodes). _scenario_roots maps a bump tag to its book
    //! node; _scenario_premium holds each bump's MC premium after Tree_Read_.
    bool _build_greek_scenarios = false; //!< set per PriceBook_: build the bump sub-trees
    bool _suppress_scenarios = false;    //!< force a base-only tree (theta reprice / fallback)
    vector<std::pair<string, MonteCarloNode*>> _scenario_roots;
    map<string, double> _scenario_premium;
    bool CanSingleTreeGreeks_() const; //!< true iff every underlying is Mono (American is handled by a frozen LSM policy)
    void BuildGreekScenarios_();       //!< build the delta/gamma/vega/rho bump sub-trees

    //! tree
    void Tree_Init_();
    void Tree_Run_();
    void Tree_Read_();

    void CreateBrownianNodes_();
    void CorrelateBrownianNodes_();
    void CreateContractualNodes_();

    void ComputeCholeskyMatrix();

    //! American path recording (opt-in : only when American contracts exist)
    void SetupAmericanRecording();
    void LogRecordings();

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
        double s0 = 0;          //!< base-path initial spot (moneyness normaliser m = S/s0)
        vector<double> tau;     //!< year fractions of the recorded exercise grid
        vector<double> b0;      //!< continuation regression constant per exercise date
        vector<double> b1;      //!< continuation regression slope (m) per exercise date
        vector<double> b2;      //!< continuation regression curvature (m^2) per exercise date
        vector<bool> has_fit;   //!< true where a continuation fit exists (enough ITM points)
    };
    void PriceAmerican();
    AmericanPolicy FitAmericanPolicy( Contract* Contract,
                                      const gsl_matrix* Paths,
                                      const vector<double>& Tau,
                                      double Rate );
    double ApplyAmericanPolicy( Contract* Contract,
                                const gsl_matrix* Paths,
                                const vector<double>& Tau,
                                double Rate,
                                const AmericanPolicy& Policy,
                                double& Trust );

  protected:
    void PreCheck_() override; //!< require an mcl_configuration and a correlation
    void PriceBook_() override;
    //! single-tree Greeks (delta/gamma/vega/rho in one path sweep); theta stays
    //! bump-and-revalue. Falls back to Pricer::ComputeGreeks_ when !CanSingleTreeGreeks_.
    void ComputeGreeks_() override;

  public:
    //! constructor, destructor
    PricerMCL( const string& ObjectName,
               YamlConfig& YamlConfig );
    ~PricerMCL() override;
};
