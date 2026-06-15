#include "thoth.hpp"
#include "bs_volatility.hpp"

//! constructor
BsVolatility::BsVolatility( const string& ObjectName ) : Volatility( ObjectName, KIND_BS_VOLATILITY )
{
    _is_local = false;
}

BsVolatility::~BsVolatility() = default;

//! setter
void BsVolatility::SetVolatility( double BsVolatility )
{
    _volatility = BsVolatility / 100;
}

//! implicit vol (+ vega bump shift, if any)
double BsVolatility::GetImplicitVol( const double /*Strike*/,
                                     const date& /*MaturityDate*/ )
{
    return ( _volatility + _vol_shift ) * GetDayWeight();
}

MonteCarloNode* BsVolatility::GetNode( NodeCollector& NC )
{
    //! the Monte-Carlo diffusion reads the vol here, so the vega shift must apply
    auto init = [&]( ConstantNode* C ) { C->SetConstantValue( _volatility + _vol_shift ); };
    //! mutualise with the base tree unless the current Greek scenario bumps vols
    if ( NC.HasScenario() && !NC.ScenarioBumpsVol() )
    {
        return NC.GetOrCreateShared<ConstantNode>( _name, init );
    }
    return NC.GetOrCreate<ConstantNode>( _name, init );
}