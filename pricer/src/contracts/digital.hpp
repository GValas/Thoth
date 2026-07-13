#pragma once
#include "contract.hpp"

//! A European binary/digital option on one underlying: cash-or-nothing (pays a fixed
//! cash amount) or asset-or-nothing (pays the spot), in both cases iff the option is in
//! the money at maturity (S_T > K for a call, S_T < K for a put). Path-independent (it
//! reads the terminal spot only), so it prices by ANA (closed form), PDE and MCL.
class Digital : public Contract
{

  private:
    double _strike = 0;                  //!< resolved cash strike K (see SetToday)
    double _strike_input = 0;            //!< configured strike (cash, or % of spot)
    bool _is_absolute_strike = true;     //!< false: strike is a percent of the spot
    date _maturity_date;                 //!< expiry / single settlement date
    OptionType _type = OptionType::Call; //!< Call vs Put
    bool _is_cash = true;                //!< cash-or-nothing (true) vs asset-or-nothing
    double _cash_amount = 1;             //!< fixed cash payout Q (cash-or-nothing only)

  public:
    //! read own fields (strike / maturity / type / payout / cash_amount)
    void Configure( ObjectReader& reader ) override;

    //! roll the valuation date, then resolve a relative strike against the base
    //! (unbumped) spot so the cash strike stays fixed through Greek bumps
    void SetToday( const date& Today ) override;

    //! getter
    [[nodiscard]] double GetStrike() const;
    [[nodiscard]] OptionType GetType() const;
    [[nodiscard]] bool IsCashOrNothing() const;
    [[nodiscard]] double GetCashAmount() const;
    date GetMaturityDate() const override;

    //! mcl node — a DigitalFlowNode (binary payoff at maturity)
    MonteCarloNode* GetFlowNode( NodeCollector& NC,
                                 const date& AsOfDate ) override;

    //! trade properties (intrinsic payoff + exercise style)
    double Intrinsic( const double spot ) override;
    bool IsAmerican() override;
    //! LSM moneyness normaliser: the strike (the payoff jump sits there)
    double LsmBasisNorm( double PathInitialSpot ) override
    {
        return ( GetStrike() > 0 ) ? GetStrike() : PathInitialSpot;
    }

    //! fixing dates (single observation at maturity)
    set<date> GetFixingDates() override;
    set<date> GetFlowDates() override;

    //! constructeur / destructeur
    Digital( const string& ObjectName );
    ~Digital() override;
};
