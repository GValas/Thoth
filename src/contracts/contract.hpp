#pragma once
#include "underlying.hpp"

//! GPU Monte-Carlo (mcl_gpu) parameters for a European vanilla under geometric
//! Brownian motion — the forward-measure scalars (the same ones the analytic BS
//! pricer uses). Filled by Contract::GPU_GbmParams for GPU-supported contracts.
struct GpuGbmParams
{
    double forward = 0; //!< carries the carry / dividend / quanto drift
    double strike = 0;
    double t = 0;   //!< year fraction today -> maturity
    double vol = 0; //!< implied vol at (strike, maturity)
    double df = 0;  //!< discount factor to maturity
    bool is_call = true;
};

class Contract : public Object
{

  protected:
    //! premium
    double _premium = 0;
    double _premium_trust = 0;

    //! greeks
    double _delta = 0;
    double _gamma = 0;
    double _vega_bs = 0;
    double _volga_bs = 0;

    //! bump-and-revalue Greeks (per contract), filled by the PDE/ANA engines so
    //! the book totals can be attributed back to each contract. _delta/_gamma
    //! above double as the bump spot Greeks; these add the rest.
    double _vega = 0;  //!< premium change per 1 vol point (0.01 of vol)
    double _rho = 0;   //!< premium change per 1% (0.01) parallel rate move
    double _theta = 0; //!< premium change over one calendar day

    //! for relative outputs
    double RelativeFactor();

    //! attributes
    Underlying* _underlying = nullptr;
    Currency* _premium_currency = nullptr;
    Correlation* _correlation = nullptr; //! for quanto purposes

    // mcl indexes
    vector<int> vect_idx_flow_date;   // flow dates
    vector<int> vect_idx_fixing_date; // fixing dates
    int idx_underlying = 0;           // underling position
    int idx_contract = 0;             // contract position

  public:
    //! setter
    void SetUnderlying( Underlying& underlying );
    void SetPremiumCurrency( Currency& premium_currency );
    void SetPremium( double Premium );
    void SetPremiumTrust( double PremiumTrust );
    void SetDelta( double Delta );
    void SetGamma( double Delta );
    void SetVega( double Vega );
    void SetRho( double Rho );
    void SetTheta( double Theta );
    void SetToday( const date& Today ) override;
    void SetCorrelation( Correlation* Correlation );

    //! getter
    Underlying* GetUnderlying();
    Currency* GetPremiumCurrency();

    SingleSet GetSingleSet();

    double GetPremium();
    double GetPremiumTrust();
    double GetRelativePremium();
    double GetDelta();
    double GetGamma();
    double GetVega();
    double GetRho();
    double GetTheta();
    double GetVegaBS();
    double GetVolgaBS();

    //! mcl nodes
    MonteCarloNode* GetNode( NodeCollector& NC );
    MonteCarloNode* GetUnderlyingNode( NodeCollector& NC );
    virtual MonteCarloNode* GetFlowNode( NodeCollector& NC, const date& AsOfDate ) = 0;

    virtual date GetMaturityDate() = 0;

    //! pde flow evaluation
    virtual bool PDE_HasSolution() = 0;
    virtual double PDE_EvalFlow( const double spot ) = 0;
    virtual bool PDE_IsAmerican() = 0;

    //! pde knock-out / knock-in barrier (continuous monitoring).
    //! Non-barrier contracts keep the defaults below.
    virtual bool PDE_IsBarrier()
    {
        return false;
    }
    virtual bool PDE_IsKnockIn()
    {
        return false;
    }
    virtual bool PDE_IsUpBarrier()
    {
        return false;
    }
    virtual bool PDE_IsDiscreteBarrier()
    {
        return false;
    }
    virtual double PDE_BarrierLevel()
    {
        return 0;
    }

    //! analytical
    virtual bool ANA_HasSolution() = 0;
    virtual void ANA_EvalPrice() = 0;

    //! GPU Monte-Carlo (mcl_gpu): fill Out and return true iff this contract is a
    //! GPU-supported European vanilla under (deterministic-vol) GBM; false for
    //! American / barrier / stochastic-vol / multi-asset, so PricerMCLGpu falls
    //! back to the CPU MCL engine. Default: unsupported.
    virtual bool GPU_GbmParams( GpuGbmParams& /*Out*/ )
    {
        return false;
    }

    //! fixing dates
    virtual set<date> GetFixingDates() = 0;
    virtual set<date> GetFlowDates() = 0;
    virtual set<date> GetAmericanExerciseDates() = 0;

    //! constructor
    Contract( const string& ObjectName,
              const string& ObjectKind );
    ~Contract() override;
};
