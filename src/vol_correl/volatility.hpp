#pragma once
#include "market_data.hpp"
#include "node_collector.hpp"

class Volatility : public MarketData
{

  private:
    double _non_working_days_weight;

  protected:
    double GetDayWeight();

    //! additive parallel shift (in vol units) applied on top of the quoted vol;
    //! used by the bump-and-revalue vega. Zero in normal pricing.
    double _vol_shift = 0.0;

  public:
    // setter
    void SetNonWorkingDaysWeight( double Weight );

    //! set the parallel vol shift (vega bump); 0 restores the quoted surface
    void SetVolShift( double Shift ) { _vol_shift = Shift; }

    //
    bool _is_local;

    //! true for a genuine stochastic-volatility model (Heston): the MCL engine
    //! builds a dedicated variance + spot diffusion instead of the constant-vol
    //! SpotDiffusionNode. Deterministic vols (bs / sabr) return false.
    virtual bool IsStochastic() const { return false; }

    // local & implicit vols. Forward is the underlying's forward to MaturityDate:
    // SABR (a forward-measure model) evaluates Hagan's formula at it; the flat
    // bs/heston surfaces ignore it. Callers that only know a spot may pass that.
    virtual double GetImplicitVol( const double Strike,
                                   const double Forward,
                                   const date& MaturityDate ) = 0;

    double GetLocalVolatility( const double Strike,
                               const date& MaturityDate,
                               const double Spot,
                               const double RiskFreeRate,
                               const double ContinuousDividend );

    //! mcl node
    virtual MonteCarloNode* GetNode( NodeCollector& NC ) = 0;

    //! constructor & destructor
    Volatility( const string& ObjectName,
                const string& ObjectKind );

    ~Volatility() override;
};
