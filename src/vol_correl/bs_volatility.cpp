//! bs_volatility.cpp — flat Black-Scholes volatility surface.
//!
//! A single constant vol, independent of strike and maturity, scaled only by the
//! calendar day-weight. Being constant it is *exact* in every engine (no Dupire
//! approximation), and feeds the analytic / PDE / Monte-Carlo pricers identically.

#include "thoth.hpp"
#include "bs_volatility.hpp"
#include "object_reader.hpp"

//! constructor: register as a BS-vol kind; deterministic so not a local-vol
//! surface (no Dupire build) — the constant vol is consumed directly.
BsVolatility::BsVolatility( const string& ObjectName ) : Volatility( ObjectName, KIND_BS_VOLATILITY )
{
    _is_local = false; //!< flat surface: PDE uses the vol as-is, not via GetLocalVolatility
}

BsVolatility::~BsVolatility() = default;

//! read own field (flat vol), then the common calendar
void BsVolatility::Configure( ObjectReader& reader )
{
    Volatility::Configure( reader );                        //!< common fields first (optional calendar)
    _volatility = reader.Get<double>( "volatility" ) / 100; //!< quoted percent -> decimal (20 -> 0.20)
}

//! implied vol at any (strike, forward, maturity) — all three are ignored since
//! the surface is flat. Returns (quoted vol + parallel vega bump) re-scaled by
//! the calendar day-weight; _vol_shift is 0 in normal pricing.
double BsVolatility::GetImplicitVol( const double /*Strike*/,
                                     const double /*Forward*/,
                                     const date& /*MaturityDate*/ )
{
    return ( _volatility + _vol_shift ) * GetDayWeight();
}

MonteCarloNode* BsVolatility::GetNode( NodeCollector& NC )
{
    //! the Monte-Carlo diffusion reads the vol here, so the vega shift must apply
    //! — and the calendar day-weight too, so MCL matches the ANA/PDE vol (which
    //! use GetImplicitVol = (vol + shift) * GetDayWeight()). No-op when weight = 1.
    auto init = [&]( ConstantNode* C )
    { C->SetConstantValue( ( _volatility + _vol_shift ) * GetDayWeight() ); };
    //! mutualise with the base tree unless the current Greek scenario bumps vols
    if ( NC.HasScenario() && !NC.ScenarioBumpsVol() )
    {
        return NC.GetOrCreateShared<ConstantNode>( _name, init );
    }
    return NC.GetOrCreate<ConstantNode>( _name, init );
}