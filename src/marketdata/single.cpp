#include "thoth.hpp"
#include "single.hpp"
#include "heston_volatility.hpp"

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
    //! supply the underlying's forward so a forward-measure surface (SABR) is
    //! evaluated correctly; flat surfaces ignore it
    return _volatility->GetImplicitVol( Strike, GetForward( MaturityDate ), MaturityDate );
}

//! mcl nodes
MonteCarloNode* Single::GetNode( NodeCollector& NC )

{
    //! stochastic vol (Heston): a dedicated variance + spot diffusion (Andersen
    //! QE) instead of the constant-vol GBM step. Reuses the shared "#white_noise"
    //! for the spot's independent Gaussian and "#vol_white_noise" for the variance.
    if ( _volatility->IsStochastic() )
    {
        HestonVolatility* h = dynamic_cast<HestonVolatility*>( _volatility );
        MonteCarloNode* var = NC.GetOrCreate<HestonVarianceNode>(
            _name + "#variance",
            [&]( HestonVarianceNode* V )
            {
                V->SetParameters( h->GetV0(), h->GetKappa(), h->GetTheta(), h->GetXi() );
                V->SetNoiseNode( NC.GetNode( _name + "#vol_white_noise" ) );
            } );
        return NC.GetOrCreate<HestonSpotNode>(
            _name + "#spot",
            [&]( HestonSpotNode* S )
            {
                S->SetParameters( h->GetKappa(), h->GetTheta(), h->GetXi(), h->GetRho() );
                S->SetVarianceNode( var );
                S->SetDriftNode( GetDriftNode( NC ) );
                S->SetNoiseNode( NC.GetNode( _name + "#white_noise" ) );
                //! Bates : wire the jump source if this Heston vol carries jumps
                if ( h->HasJumps() )
                {
                    S->SetJumpNode( NC.GetNode( _name + "#jump_noise" ) );
                }
                S->SetSpot( _spot );
            } );
    }

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