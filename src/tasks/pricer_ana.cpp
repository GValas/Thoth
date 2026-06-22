#include "thoth.hpp"
#include "pricer_ana.hpp"
#include "cancellation.hpp"
#include "progress_bar.hpp"

//! a closed-form pricer is just a Pricer; no engine state to initialise
PricerANA::PricerANA( const string& ObjectName,
                      YamlConfig& YamlConfig ) : Pricer( ObjectName, YamlConfig )
{
}

PricerANA::~PricerANA() = default;

//! check that closed-form resolution is allowed for every contract
void PricerANA::PreCheck()
{
    CheckAllowed( []( Contract* c )
                  { return c->ANA_HasSolution(); }, "ANA (closed-form)" );
}

//! price the whole book by closed-form. One progress bar over the contracts;
//! each step prices the contract and, when requested, its bump-and-revalue
//! Greeks (see Pricer::PriceBookByContract).
void PricerANA::PriceBook()
{
    PriceBookByContract( "ANA" );
}

//! single-contract closed-form price hook used by the per-contract loop / Greeks
void PricerANA::PriceContract( Contract* Ctr )
{
    Ctr->ANA_EvalPrice();
}
