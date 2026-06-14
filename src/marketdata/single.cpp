#include "thoth.hpp"
#include "single.hpp"

//! constructor
Single::Single( const string& ObjectName,
                const string& ObjectKind ) : Asset( ObjectName, ObjectKind )
{
    _volatility = nullptr;
}

//! destructor
Single::~Single() = default;

//! setter
void Single::SetVolatility( Volatility& Volatility )
{
    _volatility = &Volatility;
}

//! setter
void Single::SetToday( const date& Today )
{
    _volatility->SetToday( Today );
    Asset::SetToday( Today );
}

//! setter
void Single::SetSpot( double Spot )
{
    _spot = Spot;
}

//! getter
Volatility* Single::GetVolatility()
{
    return _volatility;
}

//! getter
double Single::GetSpot()
{
    return _spot;
}

//! implicit vol
double Single::GetImplicitVol( const double Strike,
                               const date& MaturityDate )
{
    return _volatility->GetImplicitVol( Strike, MaturityDate );
}

//! mcl nodes
MonteCarloNode* Single::GetNode( NodeCollector& NC )

{
    return NC.GetOrCreate<SpotDiffusionNode>(
        _name + "#spot",
        [&]( SpotDiffusionNode* S )
        {
            S->SetBrownianNode( NC.GetNode( _name + "#brownian" ) );
            S->SetDriftNode( GetDriftNode( NC ) );
            S->SetLocalVolNode( GetVolNode( NC ) );
            S->SetSpot( _spot );
        } );
}

MonteCarloNode* Single::GetVolNode( NodeCollector& NC )
{
    return _volatility->GetNode( NC );
}