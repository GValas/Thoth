#pragma once
#include "market_data.hpp"
#include "node_collector.hpp"
#include <map>

//! The stochastic-vol (Heston / Bates) parameters an engine needs to price the
//! model, exposed polymorphically so the engines never name a concrete vol class
//! (no dynamic_cast<HestonVolatility*>). All-zero for a deterministic surface.
//! Carries the *current* values, including any vega / vega_<param> bump shifts, so
//! bump-and-revalue Greeks keep working.
struct StochasticVolParams
{
    double v0 = 0, kappa = 0, theta = 0, xi = 0, rho = 0;   //!< Heston
    double jump_intensity = 0, jump_mean = 0, jump_vol = 0; //!< Bates (0 -> pure Heston)
    [[nodiscard]] bool has_jumps() const { return jump_intensity > 0; }
};

//! Abstract volatility surface: returns the implied vol at a (strike, forward,
//! maturity) and the Dupire local vol derived from it; concrete kinds are
//! bs_volatility (flat), sabr_volatility (Hagan) and heston_volatility.
class Volatility : public MarketData
{

  private:
    double _non_working_days_weight;

  protected:
    double GetDayWeight();

    //! additive parallel shift (in vol units) applied on top of the quoted vol;
    //! used by the bump-and-revalue vega. Zero in normal pricing.
    double _vol_shift = 0.0;

    //! additive shift per named model parameter, used by the vega_<param> Greeks
    //! (e.g. vega_alpha bumps the SABR alpha, vega_kappa the Heston kappa). Empty
    //! in normal pricing; a derived surface reads it in its parameter accessors.
    std::map<string, double> _param_shift;
    double ParamShift( const string& Name ) const
    {
        auto it = _param_shift.find( Name );
        return ( it == _param_shift.end() ) ? 0.0 : it->second;
    }

  public:
    // setter
    void SetNonWorkingDaysWeight( double Weight );

    //! set the parallel vol shift (vega bump); 0 restores the quoted surface
    void SetVolShift( double Shift ) { _vol_shift = Shift; }

    //! does this surface expose a model parameter of this name (alpha, kappa, ...)?
    //! Default: none. Stochastic / local-vol surfaces override to list theirs.
    [[nodiscard]] virtual bool HasParam( const string& /*Name*/ ) const { return false; }

    //! additive bump on a named model parameter (the vega_<param> Greeks); 0
    //! restores it. Reading is via ParamShift in the derived accessors.
    void SetParamShift( const string& Name, double Shift ) { _param_shift[Name] = Shift; }

    //
    bool _is_local;

    //! true for a genuine stochastic-volatility model (Heston): the MCL engine
    //! builds a dedicated variance + spot diffusion instead of the constant-vol
    //! SpotDiffusionNode. Deterministic vols (bs / sabr) return false.
    [[nodiscard]] virtual bool IsStochastic() const { return false; }

    //! stochastic-vol (Heston / Bates) parameters for the engines; default empty.
    //! Override in a stochastic-vol surface; every engine reads this instead of
    //! down-casting to the concrete model.
    virtual StochasticVolParams StochasticParams() const { return {}; }

    //! inject the spot/variance correlation (resolved from the global correlation
    //! matrix) onto a stochastic-vol surface; no-op for deterministic surfaces.
    virtual void SetStochasticRho( double /*Rho*/ ) {}

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
