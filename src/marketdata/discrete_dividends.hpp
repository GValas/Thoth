#pragma once
#include "market_data.hpp"

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

    //! setter
    void SetDates( const vector<date>& Dates );
    void SetAmounts( const vector<double>& Amounts );

    //! getter
    const vector<date>& GetDates() const;
    const vector<double>& GetAmounts() const;

    //! constructor, destructor
    DiscreteDividends( const string& ObjectName );
    ~DiscreteDividends() override;
};
