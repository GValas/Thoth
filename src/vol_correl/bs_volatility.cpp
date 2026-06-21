#include "thoth.hpp"
#include "bs_volatility.hpp"
#include "object_reader.hpp"

//! constructor
BsVolatility::BsVolatility( const string& ObjectName ) : Volatility( ObjectName, KIND_BS_VOLATILITY )
{
    _is_local = false;
}

BsVolatility::~BsVolatility() = default;

//! read own field (flat vol), then the common calendar
void BsVolatility::Configure( ObjectReader& reader )
{
    SetVolatility( reader.Get<double>( "volatility" ) );
    ConfigureCommon( reader );
}

//! setter
void BsVolatility::SetVolatility( double BsVolatility )
{
    _volatility = BsVolatility / 100;
}

//! implicit vol (+ vega bump shift, if any)
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