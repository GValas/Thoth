//! leverage_surface.cpp — calibrated LSV leverage surface L(S,t) (see header).

#include "thoth.hpp"
#include "leverage_surface.hpp"

void LeverageSurface::PushLayer( double T,
                                 double LnStep,
                                 long Offset,
                                 std::vector<double> Levels )
{
    _t_list.push_back( T );
    _ln_step_list.push_back( LnStep );
    _offset_list.push_back( Offset );
    _level_list.push_back( std::move( Levels ) );
}

//! clamped linear interpolation on layer K's log-spot grid — the exact lookup the
//! MCL LocalVolatilityNode performs, so MC and PDE read the same surface
double LeverageSurface::LayerAt( size_t K, double S ) const
{
    const std::vector<double>& lev = _level_list[K];
    const double ln_step = _ln_step_list[K];
    const long offset = _offset_list[K];

    //! fractional grid index (grid point i sits at log-spot = (offset + i) * ln_step);
    //! S <= 0 maps to -inf and clamps to the first point
    const double f = log( S ) / ln_step - (double)offset;
    const size_t last = lev.size() - 1;
    if ( !( f > 0.0 ) ) //!< also catches NaN
    {
        return lev[0];
    }
    if ( f >= (double)last )
    {
        return lev[last];
    }
    const size_t i = (size_t)f;
    const double w = f - (double)i;
    return ( 1.0 - w ) * lev[i] + w * lev[i + 1];
}

//! linear in time between the two bracketing layers, flat beyond the ends
double LeverageSurface::GetLeverage( double S, double T ) const
{
    if ( _t_list.empty() )
    {
        return 1.0;
    }
    if ( T <= _t_list.front() )
    {
        return LayerAt( 0, S );
    }
    if ( T >= _t_list.back() )
    {
        return LayerAt( _t_list.size() - 1, S );
    }
    //! first layer with time >= T (bounded by the checks above)
    size_t hi = 1;
    while ( _t_list[hi] < T )
    {
        hi++;
    }
    const size_t lo = hi - 1;
    const double w = ( T - _t_list[lo] ) / ( _t_list[hi] - _t_list[lo] );
    return ( 1.0 - w ) * LayerAt( lo, S ) + w * LayerAt( hi, S );
}
