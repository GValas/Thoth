#include <format>
#include "thoth.hpp"
#include "termsheet.hpp"
#include "autocallable.hpp"
#include "barrier.hpp"
#include "currency.hpp"
#include "enums.hpp"
#include "misc.hpp"
#include "object_reader.hpp"
#include "vanilla.hpp"
#include "variance_swap.hpp"

//! termsheet.cpp — render one contract's YAML description as a Markdown
//! termsheet (see the header). Pure documentation: no pricing, no Greeks —
//! the levels shown are the same cash resolutions the engines price with.

namespace
{
//! compact number rendering for document text (no trailing zeros)
string Num( double v )
{
    return std::format( "{:g}", v );
}

//! ISO date rendering (YYYY-MM-DD)
string Iso( const date& d )
{
    return boost::gregorian::to_iso_extended_string( d );
}

//! one "| field | value |" table row
string Row( const string& Field, const string& Value )
{
    return "| " + Field + " | " + Value + " |\n";
}
} // namespace

Termsheet::Termsheet( const string& ObjectName,
                      YamlConfig& YamlConfig ) : Task( ObjectName, YamlConfig, KIND_TERMSHEET )
{
}

Termsheet::~Termsheet() = default;

//! read own fields: the contract, the as-of date, the result block name, and
//! the optional title / issuer lines
void Termsheet::Configure( ObjectReader& reader )
{
    _contract = reader.Ref<Contract>( "contract" );
    _as_of = reader.Get<date>( "today" );
    SetResult( reader.Get<string>( "result" ) );
    _title = reader.Get<string>( "title", "" );
    _issuer = reader.Get<string>( "issuer", "" );
}

//! the payoff clause for the concrete contract flavour. The dynamic_cast
//! dispatch mirrors the engines (a contract is a pure description; what a
//! consumer does with it is the consumer's decision).
string Termsheet::PayoffSection() const
{
    std::ostringstream o;
    const string udl = _contract->GetUnderlying()->GetName();

    if ( auto* ac = dynamic_cast<Autocallable*>( _contract ) )
    {
        const size_t n = ac->GetAutocallDates().size();
        o << "On each observation date (see schedule), if the closing level of **" << udl
          << "** is at or above the autocall level of **" << Num( ac->AutocallLevel() )
          << "** the note redeems early";
        if ( ac->IsPhoenix() )
        {
            o << " at its nominal of **" << Num( ac->GetNominal() )
              << "** (that date's coupon is paid alongside).\n\n"
              << "A conditional coupon of **" << Num( ac->PeriodCoupon() )
              << "** is paid at every observation date (and at maturity) on which the note "
              << "is still outstanding and the closing level is at or above the coupon "
              << "barrier of **" << Num( ac->CouponLevel() ) << "**";
            if ( ac->HasCouponMemory() )
            {
                o << "; a paying date also recovers every consecutively missed coupon "
                     "since the last payment (memory feature)";
            }
            o << ".\n\n";
        }
        else
        {
            o << ", paying nominal plus the accrued coupon — **nominal x (1 + k x "
              << Num( 100 * ac->Rebate( 1 ) / ac->GetNominal() - 100 )
              << "%)** for the k-th observation (snowball).\n\n";
        }
        o << "If never called, at maturity the note pays:\n"
          << "- ";
        if ( ac->IsPhoenix() )
        {
            o << "the nominal of **" << Num( ac->GetNominal() ) << "**";
        }
        else
        {
            o << "**nominal x (1 + " << ( n + 1 ) << " x coupon)** if the final level is at "
              << "or above the autocall level, the bare nominal";
        }
        o << " if the final level is at or above the protection barrier of **"
          << Num( ac->ProtectionLevel() ) << "**;\n"
          << "- otherwise **nominal x S_final / " << Num( ac->ReferenceSpot() )
          << "** (the capital is at risk one-for-one below the protection barrier).\n";
        return o.str();
    }

    if ( auto* vs = dynamic_cast<VarianceSwap*>( _contract ) )
    {
        o << "At maturity the swap pays **notional x (realized variance - strike "
          << "variance)**, with a volatility strike of **"
          << Num( 100 * vs->GetVolatilityStrike() ) << "%** (strike variance "
          << Num( vs->GetVolatilityStrike() * vs->GetVolatilityStrike() )
          << ") and a variance notional of **" << Num( vs->GetNotional() ) << "**. "
          << "The realized variance is the annualized sum of squared log-returns of "
          << udl << " over the observation schedule ("
          << ( vs->IsDiscretelyObserved() ? "discrete fixings — see schedule"
                                          : "continuous observation" )
          << ").";
        if ( vs->IsSeasoned() )
        {
            o << "\n\nThis is an in-life (seasoned) swap: the observation window opened "
                 "before the valuation date, and the already-realized leg is read from "
                 "the booked historical fixings; the annualizer covers the whole window ("
              << Num( vs->GetTotalYearFraction() ) << " years).";
        }
        o << "\n";
        return o.str();
    }

    if ( auto* bar = dynamic_cast<Barrier*>( _contract ) )
    {
        const bool call = ( bar->_type == OptionType::Call );
        o << "A **" << ( bar->IsUp() ? "up" : "down" ) << "-and-" << ( bar->IsIn() ? "in" : "out" )
          << " " << ( call ? "call" : "put" ) << "** on " << udl << ": the terminal payoff "
          << "**max(" << ( call ? "S_final - K" : "K - S_final" ) << ", 0)** with strike **K = "
          << Num( bar->_strike ) << "** is " << ( bar->IsIn() ? "activated" : "extinguished" )
          << " if the underlying " << ( bar->IsUp() ? "rises to or above" : "falls to or below" )
          << " the barrier level of **" << Num( bar->Level() ) << "** on any monitored date ("
          << ( bar->IsDiscrete() ? "discrete monitoring schedule" : "continuous monitoring" )
          << ").\n";
        return o.str();
    }

    if ( auto* van = dynamic_cast<Vanilla*>( _contract ) )
    {
        const bool call = ( van->GetType() == OptionType::Call );
        o << "A **" << ( van->IsAmerican() ? "American" : "European" ) << " "
          << ( call ? "call" : "put" ) << "** on " << udl << ": pays **max("
          << ( call ? "S - K" : "K - S" ) << ", 0)** with strike **K = "
          << Num( van->GetStrike() ) << "**, exercisable "
          << ( van->IsAmerican() ? "on any business day up to and including"
                                 : "at" )
          << " the maturity date.\n";
        return o.str();
    }

    ERR( "termsheet '" + GetName() + "': no renderer for contract kind '" +
         _contract->GetKind() + "'" );
}

//! the observation / autocall schedule table; empty for contracts without one
string Termsheet::ScheduleSection() const
{
    std::ostringstream o;

    if ( auto* ac = dynamic_cast<Autocallable*>( _contract ) )
    {
        o << "## Observation schedule\n\n"
          << "| # | Observation date | Autocall condition | Early redemption |\n"
          << "|---|---|---|---|\n";
        const vector<date>& obs = ac->GetAutocallDates();
        for ( size_t k = 0; k < obs.size(); k++ )
        {
            o << "| " << ( k + 1 ) << " | " << Iso( obs[k] ) << " | S >= "
              << Num( ac->AutocallLevel() ) << " | " << Num( ac->Rebate( k + 1 ) ) << " |\n";
        }
        o << "| " << ( obs.size() + 1 ) << " | " << Iso( ac->GetMaturityDate() )
          << " | maturity | redemption profile (see payoff) |\n\n";
        return o.str();
    }

    if ( auto* vs = dynamic_cast<VarianceSwap*>( _contract ) )
    {
        if ( !vs->IsDiscretelyObserved() )
        {
            return "";
        }
        o << "## Observation schedule\n\n"
          << "Remaining variance fixings (the maturity date is always the last one):\n\n";
        for ( const date& d : vs->GetObservationDates() )
        {
            o << "- " << Iso( d ) << "\n";
        }
        o << "\n";
        return o.str();
    }

    return "";
}

//! resolve the contract's levels against the as-of date, then render the document
void Termsheet::Execute()
{
    const double t0 = WallClockSeconds();
    if ( !_contract )
    {
        ERR( "termsheet '" + GetName() + "': no contract to document" );
    }

    //! the same level resolution the engines price with (relative strikes and
    //! percent autocall levels become cash against the as-of spot)
    _contract->SetToday( _as_of );

    Underlying* u = _contract->GetUnderlying();
    const string title = !_title.empty()
                             ? _title
                             : ( _contract->GetKind() + " on " + u->GetName() );

    std::ostringstream o;
    o << "# Termsheet — " << title << "\n\n"
      << "*Indicative terms as of " << Iso( _as_of ) << " — for discussion purposes only*\n\n"
      << "| | |\n|---|---|\n";
    if ( !_issuer.empty() )
    {
        o << Row( "Issuer", _issuer );
    }
    o << Row( "Product", _contract->GetKind() )
      << Row( "Trade id", _contract->GetName() )
      << Row( "Underlying", u->GetName() + " (" + u->GetCurrency()->GetName() + ")" )
      << Row( "Underlying level (as of)", Num( u->GetSpot() ) )
      << Row( "Settlement currency", _contract->GetPremiumCurrency()->GetName() )
      << Row( "Valuation date", Iso( _as_of ) )
      << Row( "Maturity", Iso( _contract->GetMaturityDate() ) )
      << "\n## Payoff\n\n"
      << PayoffSection() << "\n"
      << ScheduleSection()
      << "## Disclaimer\n\n"
      << "This document is an indicative summary of terms generated from the booked "
         "trade description. It is not an offer, a confirmation or investment advice; "
         "the legally binding terms are those of the executed documentation.\n";

    _document = o.str();
    _task_time = TaskTime( t0 );
    LOG( "TSH", "termsheet rendered for '" + _contract->GetName() + "' (" +
                    std::to_string( _document.size() ) + " chars), task_time = " +
                    ToString( _task_time ) + " sec" );
}

//! base block (kind + task_time) plus the document as a `termsheet` literal field
void Termsheet::WriteResults()
{
    Task::WriteResults();
    WriteResult( "termsheet", _document );
}
