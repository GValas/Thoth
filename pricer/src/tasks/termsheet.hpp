#pragma once
#include "task.hpp"

class Contract;

//! A documentation task: render one contract's terms as a human-readable
//! Markdown termsheet, built purely from the YAML description (no pricing).
//!
//! The document carries the standard sections — parties/dates header,
//! underlying and indicative market levels as of the valuation date, the
//! payoff clause specific to the contract flavour (vanilla / barrier /
//! variance swap / autocallable, Athena or Phoenix), the observation schedule
//! where one exists, and a disclaimer — and is written into the task's result
//! block as a single `termsheet` field (a YAML literal block, like the MCL
//! node graphs), so it travels through batch / HTTP / cluster without any
//! side-channel file.
//!
//! Levels booked in percent of spot (relative strikes, autocall levels) are
//! shown resolved in cash against the valuation-date spot — the same
//! resolution the engines price with (Contract::SetToday).
class Termsheet : public Task
{

  private:
    Contract* _contract = nullptr; //!< the trade to document (non-owning ref)
    date _as_of;                   //!< valuation date the levels resolve against
    string _title;                 //!< optional document title override
    string _issuer;                //!< optional issuer line
    string _document;              //!< the rendered Markdown (built by Execute)

    //! the payoff clause for the concrete contract flavour (dispatch on type,
    //! the same engine-side pattern PricerPDE / PricerANA use)
    string PayoffSection() const;
    //! the observation / autocall schedule table (empty when the contract has none)
    string ScheduleSection() const;

  public:
    //! read own fields: the contract reference, the as-of date, the result
    //! block name, and the optional title / issuer lines
    void Configure( ObjectReader& reader ) override;

    //! resolve the contract's levels as of the valuation date and render the
    //! Markdown document
    void Execute() override;
    //! base block (kind + task_time) plus the `termsheet` literal field
    void WriteResults() override;

    //! constructor / destructor
    Termsheet( const string& ObjectName, YamlConfig& YamlConfig );
    ~Termsheet() override;
};
