#pragma once

//! The priced result of a contract: the premium and the bundle of Greeks the
//! pricing engines fill in. Kept separate from the contract *definition* (its
//! underlying, dates and payoff) so a contract cleanly distinguishes "what it is"
//! from "what it priced to" — the engines write into Contract::Result(), the book
//! aggregation and the output reader read it back.
//!
//! delta / gamma double as the spot Greeks the PDE/ANA bump-and-revalue fills in.
//! vega_bs / volga_bs are the closed-form sensitivities the analytic pricer
//! reports; vega / rho / theta are the per-contract bump-and-revalue Greeks the
//! PDE/ANA engines attribute back to the book.
struct Valuation
{
    //! premium and its Monte-Carlo standard error (0 for deterministic pricers)
    double premium = 0;
    double premium_trust = 0;

    //! spot Greeks (also the PDE/ANA bump delta / gamma)
    double delta = 0;
    double gamma = 0;

    //! analytic (closed-form) vega / volga reported by the ANA pricer
    double vega_bs = 0;
    double volga_bs = 0;

    //! per-contract bump-and-revalue Greeks (PDE / ANA)
    double vega = 0;  //!< premium change per 1 vol point (0.01 of vol)
    double rho = 0;   //!< premium change per 1% (0.01) parallel rate move
    double theta = 0; //!< premium change over one calendar day
};
