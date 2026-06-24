#include "thoth.hpp"
#include "variance_swap.hpp"
#include "object_reader.hpp"

//! Variance-swap implementation: configuration, the model-free static-replication
//! fair-variance (Demeterfi-Derman-Kamal-Zou) closed form, and the Monte-Carlo flow
//! node that compares realized to strike variance.

//! constructor
VarianceSwap::VarianceSwap( const string& ObjectName ) : Contract( ObjectName, KIND_VARIANCE_SWAP )
{
}

VarianceSwap::~VarianceSwap() = default;

//! read own fields, then the common contract attributes
void VarianceSwap::Configure( ObjectReader& reader )
{
    Contract::Configure( reader ); //!< common fields first (underlying, premium currency)
    _maturity_date = reader.Get<date>( "maturity" );
    //! volatility_strike is in percent (like every vol), stored as decimal
    _volatility_strike = reader.Get<double>( "volatility_strike" ) / 100.0;
    _notional = reader.Get<double>( "notional", 1 );
}

//! getter
date VarianceSwap::GetMaturityDate() const
{
    return _maturity_date;
}

//! the variance PDE has a zero terminal condition and its own (remaining-variance)
//! boundaries — there is no terminal spot payoff here, so this is just 0.
double VarianceSwap::Intrinsic( const double /*Spot*/ )
{
    return 0.0;
}

//! variance swaps settle only at maturity: no early exercise
bool VarianceSwap::IsAmerican()
{
    return false;
}

//! Monte-Carlo flow: realized variance of the simulated path vs the strike.
//! Build (or fetch) a VarianceSwapFlowNode wired to the spot node, the strike
//! variance (vol^2) and the notional, settling at maturity.
MonteCarloNode* VarianceSwap::GetFlowNode( NodeCollector& NC,
                                           const date& /*AsOfDate*/ )
{
    return NC.GetOrCreate<VarianceSwapFlowNode>( _name + node_name::FLOW,
                                                 [&]( VarianceSwapFlowNode* V )
                                                 {
                                                     V->SetSpotNode( GetUnderlyingNode( NC ) );
                                                     V->SetStrikeVariance( _volatility_strike * _volatility_strike );
                                                     V->SetNotional( _notional );
                                                     V->SetFlowDateIndex( NC.GetDateIndex( _maturity_date ) );
                                                 } );
}

//! single payment at maturity (the realized variance is accumulated by the flow
//! node over the diffusion grid; only the settlement date is exposed here)
set<date> VarianceSwap::GetFixingDates()
{
    set<date> s;
    s.insert( _maturity_date );
    return s;
}

//! same single date as the fixing
set<date> VarianceSwap::GetFlowDates()
{
    return GetFixingDates();
}
