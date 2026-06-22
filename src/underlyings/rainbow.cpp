#include "thoth.hpp"
#include "rainbow.hpp"
#include "nodes.hpp"
#include "enums.hpp"
#include "object_reader.hpp"

//! rainbow.cpp — best-of / worst-of basket. Only the Monte-Carlo path is real here:
//! GetNode builds the order-statistic node; every analytic/PDE entry point (forward,
//! vol, vol-node, correl-node) deliberately throws because a max/min payoff has no
//! single-lognormal representation for those engines to consume.

//! ctor — tag KIND_RAINBOW so dispatch routes it to the MCL engine only.
Rainbow::Rainbow( const string& ObjectName ) : Basket( ObjectName, KIND_RAINBOW )
{
}

//! components non-owning; default destruction.
Rainbow::~Rainbow() = default;

//! read the component underlyings and type, then fix the rebasing reference.
//! type ("best"/"worst") is parsed to the enum; reference spots are captured last so
//! the rebasing denominators S_i0 are the inception spots.
void Rainbow::Configure( ObjectReader& reader )
{
    SetUnderlyingList( reader.Ref<vector<Underlying>>( "underlyings" ) );
    SetType( ParseRainbowType( reader.Get<string>( "type" ) ) );
    CaptureReferenceSpots(); //!< fix the rebasing reference (S_i0) at load
}

//! setter — best-of (max) vs worst-of (min).
void Rainbow::SetType( RainbowType Type )
{
    _type = Type;
}

//! at t0 every rebased performance is 1, so max/min = 1 and the spot is 100
double Rainbow::GetSpot() const
{
    return 100;
}

//! best-of / worst-of spot node : max / min of the rebased member performances.
//! The RainbowNode is told whether to take the max (best-of) or min (worst-of) and is
//! fed each member spot node with a rebasing weight; it then forms 100*S_i/S_i0 per
//! component and selects the extreme one each path.
MonteCarloNode* Rainbow::GetNode( NodeCollector& NC )
{
    return NC.GetOrCreate<RainbowNode>(
        _name + node_name::SPOT,
        [&]( RainbowNode* R )
        {
            R->SetBest( _type == RainbowType::BestOf ); //!< true => max, false => min
            for ( size_t i = 0; i < _underlying_list.size(); i++ )
            {
                R->PushUnderlying( _underlying_list[i]->GetNode( NC ) );
                //! rebase S_i -> 100 * S_i / S_i0 against the FIXED reference S_i0,
                //! so a delta/gamma spot bump on S_i is not cancelled by it.
                R->PushWeight( 100.0 / RefSpot( i ) );
            }
        } );
}

//! a max/min payoff has no single-lognormal forward/vol, so it is MCL-only.
//! These analytic/PDE entry points throw rather than return an approximation.
double Rainbow::GetForward( const date& /*MaturityDate*/, Currency* /*QuantoCurrency*/ )
{
    ERR( "rainbow basket '" + _name + "' is only supported by the mcl engine" );
}

//! no single equivalent vol exists for an order-statistic payoff — throws.
double Rainbow::GetImplicitVol( const double /*Strike*/, const date& /*MaturityDate*/ )
{
    ERR( "rainbow basket '" + _name + "' is only supported by the mcl engine" );
}

//! no composite vol node for a rainbow — throws.
MonteCarloNode* Rainbow::GetVolNode( NodeCollector& /*NC*/ )
{
    ERR( "rainbow basket '" + _name + "' : GetVolNode not implemented" );
}

//! no quanto correlation node for a rainbow — throws.
MonteCarloNode* Rainbow::GetCorrelNode( NodeCollector& /*NC*/,
                                        const string& /*UnderlyingCurrency*/,
                                        const string& /*BaseCurrency*/ )
{
    ERR( "rainbow basket '" + _name + "' : GetCorrelNode not implemented" );
}
