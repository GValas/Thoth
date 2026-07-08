//! lsv_volatility.cpp — local-stochastic volatility (Heston + calibrated leverage).
//!
//! Configuration and implied-vol delegation only: the leverage calibration lives
//! in Single::CalibrateLeverage (it needs the underlying's forward / Dupire
//! inputs) and the diffusion in the engines. See lsv_volatility.hpp.

#include "thoth.hpp"
#include "lsv_volatility.hpp"
#include "object_reader.hpp"

//! constructor: a stochastic surface with the LSV kind tag (IsStochastic() is
//! inherited true; _is_local stays false — the leverage grid is built by the
//! engines, not by the Dupire local-vol path of the deterministic surfaces)
LsvVolatility::LsvVolatility( const string& ObjectName )
    : HestonVolatility( ObjectName, KIND_LSV_VOLATILITY )
{
}

LsvVolatility::~LsvVolatility() = default;

//! read the Heston fields (spot / init_vol / long_vol / kappa / vol_of_vol),
//! then the target-surface reference. Bates jumps are meaningless under LSV
//! (the leverage calibration matches the pure-diffusion Dupire density), so a
//! jump field is a configuration error, not silently ignored.
void LsvVolatility::Configure( ObjectReader& reader )
{
    HestonVolatility::Configure( reader );
    if ( HasJumps() )
    {
        ERR( "lsv volatility '" + _name + "': Bates jumps are not supported under LSV" );
    }
    _surface = reader.Ref<Volatility>( "surface" );
    if ( _surface->IsStochastic() )
    {
        ERR( "lsv volatility '" + _name + "': the target surface '" + _surface->GetName() +
             "' must be a deterministic implied surface (bs / sabr)" );
    }
}

//! the target surface is referenced only from this object, so the usual
//! Single::SetToday -> volatility chain would leave it undated; forward the date
void LsvVolatility::SetToday( const date& Today )
{
    HestonVolatility::SetToday( Today );
    if ( _surface )
    {
        _surface->SetToday( Today );
    }
}

//! calibrated-model implied vol == target implied vol; the parallel vega bump is
//! applied HERE (this object is the underlying's vol, so it carries the shift)
//! rather than on the shared target object.
double LsvVolatility::GetImplicitVol( const double Strike,
                                      const double Forward,
                                      const date& MaturityDate )
{
    return _surface->GetImplicitVol( Strike, Forward, MaturityDate ) + _vol_shift;
}
