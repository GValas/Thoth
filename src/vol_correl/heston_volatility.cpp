#include "thoth.hpp"
#include "heston_volatility.hpp"

HestonVolatility::HestonVolatility( const string& ObjectName )
    : Volatility( ObjectName, KIND_HESTON_VOLATILITY )
{
    _is_local = false;
}

HestonVolatility::~HestonVolatility() = default;

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
