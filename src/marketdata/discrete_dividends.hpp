#pragma once
#include "market_data.hpp"

//! discrete_dividends.hpp — the discrete cash-dividend schedule (a data holder).
//!
//! A schedule of discrete cash dividends: an (ex-date, amount) list in the equity's
//! currency. Priced by the escrowed-dividend model — the holder (Equity) subtracts
//! the present value of the dividends due before a horizon from the spot when it
//! builds the forward (and the MCL diffusion's initial spot), so the same escrowed
//! forward feeds the ANA, PDE and MCL engines. This object is a plain data holder;
//! the present-value discounting (which needs the equity's discount curve) lives in
//! Equity.
class DiscreteDividends : public MarketData
{
  private:
    vector<date> _dates;     //!< ex-dividend dates (ascending)
    vector<double> _amounts; //!< cash amount paid at each date (equity currency)

  public:
    //! read own fields (ex-dates + cash amounts)
    void Configure( ObjectReader& reader ) override;

    //! setter — the ascending ex-dividend dates
    void SetDates( const vector<date>& Dates );
    //! setter — the cash amounts (index-aligned with the ex-dates)
    void SetAmounts( const vector<double>& Amounts );

    //! getter — ex-dividend dates (used by Equity to time the escrow PV)
    const vector<date>& GetDates() const;
    //! getter — cash amounts per ex-date (Equity discounts these on its curve)
    const vector<double>& GetAmounts() const;

    //! constructor, destructor
    DiscreteDividends( const string& ObjectName );
    ~DiscreteDividends() override;
};
