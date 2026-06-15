#include "thoth.hpp"
#include "rainbow.hpp"
#include "nodes.hpp"

//!
Rainbow::Rainbow( const string& ObjectName ) : Basket( ObjectName, KIND_RAINBOW )
{
}

//!
Rainbow::~Rainbow() = default;

//! setter
void Rainbow::SetType( RainbowType Type )
{
    _type = Type;
}

//! at t0 every rebased performance is 1, so max/min = 1 and the spot is 100
double Rainbow::GetSpot()
{
    return 100;
}

//! best-of / worst-of spot node : max / min of the rebased member performances
MonteCarloNode* Rainbow::GetNode( NodeCollector& NC )
{
    return NC.GetOrCreate<RainbowNode>(
        _name + "#spot",
        [&]( RainbowNode* R )
        {
            R->SetBest( _type == RainbowType::BestOf );
            for ( Underlying* u : _underlying_list )
            {
                R->PushUnderlying( u->GetNode( NC ) );
                R->PushWeight( 100.0 / u->GetSpot() ); //!< rebase S_i -> 100 * S_i/S_i0
            }
        } );
}

//! a max/min payoff has no single-lognormal forward/vol, so it is MCL-only
double Rainbow::GetForward( const date& /*MaturityDate*/, Currency* /*QuantoCurrency*/ )
{
    ERR( "rainbow basket '" + _name + "' is only supported by the mcl engine" );
}

double Rainbow::GetImplicitVol( const double /*Strike*/, const date& /*MaturityDate*/ )
{
    ERR( "rainbow basket '" + _name + "' is only supported by the mcl engine" );
}

MonteCarloNode* Rainbow::GetVolNode( NodeCollector& /*NC*/ )
{
    ERR( "rainbow basket '" + _name + "' : GetVolNode not implemented" );
}

MonteCarloNode* Rainbow::GetCorrelNode( NodeCollector& /*NC*/,
                                        const string& /*UnderlyingCurrency*/,
                                        const string& /*BaseCurrency*/ )
{
    ERR( "rainbow basket '" + _name + "' : GetCorrelNode not implemented" );
}
