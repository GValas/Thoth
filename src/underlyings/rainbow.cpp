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
            for ( size_t i = 0; i < _underlying_list.size(); i++ )
            {
                R->PushUnderlying( _underlying_list[i]->GetNode( NC ) );
                //! rebase S_i -> 100 * S_i / S_i0 against the FIXED reference S_i0,
                //! so a delta/gamma spot bump on S_i is not cancelled by it.
                R->PushWeight( 100.0 / RefSpot( i ) );
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
