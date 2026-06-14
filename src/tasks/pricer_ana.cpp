#include "thoth.hpp"
#include "pricer_ana.hpp"
#include "cancellation.hpp"
#include "progress_bar.hpp"

PricerANA::PricerANA( const string& ObjectName,
                      YamlConfig& YamlConfig ) : Pricer( ObjectName, YamlConfig )
{
}

PricerANA::~PricerANA() = default;

//! check that closed-form resolution is allowed for every contract
void PricerANA::PreCheck_()
{
    CheckAllowed( []( Contract* c )
                  { return c->ANA_HasSolution(); }, "ANA (closed-form)" );
}

//! price the whole book by closed-form. One progress bar over the contracts;
//! each step prices the contract and, when requested, its bump-and-revalue
//! Greeks (see Pricer::PriceBookByContract_).
void PricerANA::PriceBook_()
{
    PriceBookByContract_( "ANA" );
}

//! single-contract closed-form price hook used by the per-contract loop / Greeks
void PricerANA::PriceContract_( Contract* Ctr )
{
    Ctr->ANA_EvalPrice();
}
