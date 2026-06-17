#pragma once
#include "underlying.hpp"
#include "valuation.hpp"

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
    //! the priced result (premium + Greeks); engines write it via Result()
    Valuation _valuation;

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
    void SetToday( const date& Today ) override;
    void SetCorrelation( Correlation* Correlation );

    //! getter
    Underlying* GetUnderlying();
    Currency* GetPremiumCurrency();

    SingleSet GetSingleSet();

    //! the priced result (premium + Greeks): engines fill it, aggregation reads it
    Valuation& Result() { return _valuation; }
    const Valuation& Result() const { return _valuation; }

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

    //! priced on the spot grid as the expected accumulated variance (a backward
    //! PDE with a local-variance source) rather than a terminal-payoff solve.
    //! Only the variance swap overrides this; it routes the PDE pricer to the
    //! dedicated accumulated-variance solve.
    virtual bool PDE_IsAccruedVariance()
    {
        return false;
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
