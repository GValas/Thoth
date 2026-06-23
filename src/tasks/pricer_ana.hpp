#pragma once
#include "pricer.hpp"

//! Book pricing via closed-form (analytic) formulas.
//!
//! The closed-form mathematics lives HERE, in the task — the contracts are pure
//! descriptions (strike / type / barrier flavour / notional + getters). PriceContract
//! dispatches on the contract type and evaluates the matching formula (Black-Scholes
//! or Heston for vanillas, Reiner-Rubinstein for barriers, the Demeterfi-Derman-
//! Kamal-Zou static-replication strip for variance swaps) into the pricer's result.
//! Whether a contract HAS a closed form is still asked of the contract
//! (ANA_HasSolution, a capability predicate); no PDE grid is solved.

class Vanilla;
class Barrier;
class VarianceSwap;

class PricerANA : public Pricer
{

  protected:
    void PreCheck() override; //!< require a closed-form solution for every contract
    void PriceBook() override;
    void PriceContract( Contract* Ctr ) override; //!< single-contract closed-form price
    bool GreeksPerContract() const override { return true; }

  private:
    //! per-type closed forms, each writing premium (+ any analytic Greeks) into the
    //! contract's Result() entry. Spot/forward/vol come from the (rolled) underlying.
    void PriceVanilla( Vanilla* Opt );
    void PriceBarrier( Barrier* Bar );
    void PriceVarianceSwap( VarianceSwap* Swap );

    //! Reiner-Rubinstein closed-form barrier price for a single spot value (used for
    //! the premium and the finite-difference spot Greeks). r is the risk-free rate,
    //! b the cost-of-carry, v the vol, t the year fraction, df = exp(-r t).
    double BarrierPrice( Barrier* Bar, double S, double r, double b, double v, double t, double df );

  public:
    //! constructor, destructor
    PricerANA( const string& ObjectName,
               YamlConfig& YamlConfig );
    ~PricerANA() override;
};
