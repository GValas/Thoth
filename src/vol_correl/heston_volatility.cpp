//! heston_volatility.cpp — Heston / Bates stochastic-volatility surface.
//!
//! Stores the scalar Heston parameters (v0, kappa, theta, xi, rho) plus optional
//! lognormal Bates jumps, reading vols quoted in percent and squaring them into
//! variances. The actual stochastic diffusion is built by the engines (they read
//! StochasticParams()); this object mainly configures, exposes and bump-shifts the
//! parameters. GetImplicitVol is only a coarse sqrt(v0) proxy for legacy callers.

#include "thoth.hpp"
#include "heston_volatility.hpp"
#include "object_reader.hpp"

//! constructor: register as the Heston kind. _is_local = false because Heston is
//! genuinely stochastic (IsStochastic() == true) — the engine builds a variance
//! diffusion, not a Dupire local-vol surface.
HestonVolatility::HestonVolatility( const string& ObjectName )
    : Volatility( ObjectName, KIND_HESTON_VOLATILITY )
{
    _is_local = false;
}

HestonVolatility::~HestonVolatility() = default;

//! read own fields (Heston + optional Bates jumps), then the common calendar
void HestonVolatility::Configure( ObjectReader& reader )
{
    SetSpot( reader.Get<double>( "spot" ) );
    //! vols quoted in percent (like every vol) -> variances
    SetV0( pow( reader.Get<double>( "init_vol" ) / 100.0, 2 ) );
    SetTheta( pow( reader.Get<double>( "long_vol" ) / 100.0, 2 ) );
    SetKappa( reader.Get<double>( "kappa" ) );
    SetXi( reader.Get<double>( "vol_of_vol" ) );
    //! optional Bates jumps (absent -> 0 -> pure Heston). jump_mean / jump_vol
    //! are in log-return space; jump_intensity is the yearly jump frequency.
    SetJumpIntensity( reader.Get<double>( "jump_intensity", 0 ) );
    SetJumpMean( reader.Get<double>( "jump_mean", 0 ) );
    SetJumpVol( reader.Get<double>( "jump_vol", 0 ) );
    ConfigureCommon( reader );
}

//! coarse single-number proxy (instantaneous vol, vega-bumped): used only by
//! callers that still expect one vol (e.g. PDE grid bounds, quanto fallback).
double HestonVolatility::GetImplicitVol( const double /*Strike*/,
                                         const double /*Forward*/,
                                         const date& /*MaturityDate*/ )
{
    return ( sqrt( _v0 ) + _vol_shift ) * GetDayWeight();
}

//! Heston has no constant "vol node" — the MCL engine builds the variance/spot
//! diffusion. This constant sqrt(v0) proxy is only for incidental callers
//! (e.g. a quanto adjustment that asks an underlying for "a vol node").
MonteCarloNode* HestonVolatility::GetNode( NodeCollector& NC )
{
    return NC.GetOrCreate<ConstantNode>( _name,
                                         [&]( ConstantNode* C )
                                         { C->SetConstantValue( sqrt( _v0 ) + _vol_shift ); } );
}
