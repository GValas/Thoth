#include <format>
#include "thoth.hpp"
#include "termsheet.hpp"
#include "autocallable.hpp"
#include "asian.hpp"
#include "ratchet.hpp"
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

//! the payoff clause for the concrete contract flavour: plain-English prose
//! followed by the formal payoff in LaTeX display math ($$...$$ — rendered by
//! GitHub, KaTeX and pandoc alike). The dynamic_cast dispatch mirrors the
//! engines (a contract is a pure description; what a consumer does with it is
//! the consumer's decision).
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

        //! the formal clauses: the first-trigger time, the early redemption, the
        //! (Phoenix) coupon stream and the maturity redemption profile
        o << "\nFormally, with $S_t$ the closing level, $t_1,\\dots,t_" << n
          << "$ the observation dates, $N = " << Num( ac->GetNominal() )
          << "$ and the first autocall time\n\n"
          << "$$ \\tau \\;=\\; \\min\\{\\, t_k : S_{t_k} \\ge B_{\\mathrm{AC}} \\,\\}, \\qquad B_{\\mathrm{AC}} = "
          << Num( ac->AutocallLevel() ) << " $$\n\n";
        if ( ac->IsPhoenix() )
        {
            o << "$$ \\text{Redemption}(\\tau = t_k) \\;=\\; N \\qquad\\text{(paid at } t_k\\text{)} $$\n\n"
              << "$$ \\text{Coupon}(t_k) \\;=\\; c\\,N \\cdot \\mathbf{1}\\{\\tau \\ge t_k\\} \\cdot \\mathbf{1}\\{S_{t_k} \\ge B_{\\mathrm{cpn}}\\}"
              << ( ac->HasCouponMemory() ? " \\cdot (1 + m_k)" : "" )
              << ", \\qquad c\\,N = " << Num( ac->PeriodCoupon() )
              << ",\\; B_{\\mathrm{cpn}} = " << Num( ac->CouponLevel() ) << " $$\n\n";
            if ( ac->HasCouponMemory() )
            {
                o << "with $m_k$ the number of consecutively missed coupons since the last "
                     "paying observation (memory).\n\n";
            }
            o << "$$ \\text{Redemption}(T) \\;=\\; \\begin{cases} N & S_T \\ge B_{\\mathrm{prot}} \\\\[2pt] "
                 "N \\, S_T / S_{\\mathrm{ref}} & S_T < B_{\\mathrm{prot}} \\end{cases}, "
                 "\\qquad B_{\\mathrm{prot}} = "
              << Num( ac->ProtectionLevel() ) << ",\\; S_{\\mathrm{ref}} = "
              << Num( ac->ReferenceSpot() ) << " $$\n";
        }
        else
        {
            o << "$$ \\text{Redemption}(\\tau = t_k) \\;=\\; N \\times (1 + k\\,c), \\qquad c = "
              << Num( 100 * ac->Rebate( 1 ) / ac->GetNominal() - 100 ) << "\\% $$\n\n"
              << "$$ \\text{Redemption}(T) \\;=\\; \\begin{cases} N\\,(1 + " << ( n + 1 )
              << "\\,c) & S_T \\ge B_{\\mathrm{AC}} \\\\[2pt] N & B_{\\mathrm{prot}} \\le S_T < B_{\\mathrm{AC}} "
                 "\\\\[2pt] N \\, S_T / S_{\\mathrm{ref}} & S_T < B_{\\mathrm{prot}} \\end{cases}, "
                 "\\qquad B_{\\mathrm{prot}} = "
              << Num( ac->ProtectionLevel() ) << ",\\; S_{\\mathrm{ref}} = "
              << Num( ac->ReferenceSpot() ) << " $$\n";
        }
        return o.str();
    }

    if ( auto* as = dynamic_cast<Asian*>( _contract ) )
    {
        const bool call = ( as->GetType() == OptionType::Call );
        const size_t n = as->GetObservationDates().size();
        o << "An **arithmetic average-price Asian " << ( call ? "call" : "put" )
          << "** on " << udl << ": at maturity it pays the option payoff on the "
          << "**average** level of " << udl << " over " << n
          << " observation dates (see schedule) rather than on the final level, "
          << "with strike **K = " << Num( as->GetStrike() ) << "** and a notional of **"
          << Num( as->GetNominal() ) << "**. Averaging damps the terminal variance, so "
          << "the Asian is cheaper than the equivalent vanilla.\n\n"
          << "Formally, with $t_1, \\dots, t_" << n << "$ the observation dates and $A$ "
          << "the arithmetic mean:\n\n"
          << "$$ A \\;=\\; \\frac{1}{" << n << "} \\sum_{i=1}^{" << n << "} S_{t_i}, "
          << "\\qquad \\text{Payoff}(T) \\;=\\; N \\cdot \\max\\!\\big( "
          << ( call ? "A - K" : "K - A" ) << ",\\; 0 \\big), \\qquad K = "
          << Num( as->GetStrike() ) << ",\\; N = " << Num( as->GetNominal() ) << " $$\n";
        return o.str();
    }

    if ( auto* rt = dynamic_cast<Ratchet*>( _contract ) )
    {
        const size_t n = rt->GetObservationDates().size();
        o << "A **ratchet (cliquet) note** on " << udl << ": at maturity it pays a "
          << "coupon built from the **period returns** of " << udl << " over "
          << ( n - 1 ) << " consecutive periods (see schedule). Each period return is "
          << "clipped to **[" << Num( 100 * rt->LocalFloor() ) << "%, "
          << Num( 100 * rt->LocalCap() ) << "%]** — a capped gain is **locked in** and "
          << "cannot be given back by a later fall — and the sum is floored at **"
          << Num( 100 * rt->GlobalFloor() ) << "%**";
        if ( rt->HasGlobalCap() )
        {
            o << " and capped at **" << Num( 100 * rt->GlobalCap() ) << "%**";
        }
        o << " (the global floor is the note's capital protection). Notional **"
          << Num( rt->GetNominal() ) << "**.\n\n"
          << "Formally, with $t_0, \\dots, t_" << ( n - 1 ) << "$ the period boundaries, "
          << "$R_i = S_{t_i}/S_{t_{i-1}} - 1$, local clip $[f, c]$ and global floor $F$"
          << ( rt->HasGlobalCap() ? " / cap $C$" : "" ) << ":\n\n"
          << "$$ \\text{Payoff}(T) \\;=\\; N \\cdot "
          << ( rt->HasGlobalCap() ? "\\min\\!\\Big( C,\\; " : "" )
          << "\\max\\!\\Big( F,\\; \\sum_{i=1}^{" << ( n - 1 )
          << "} \\min\\big( c, \\max(f, R_i) \\big) \\Big)"
          << ( rt->HasGlobalCap() ? " \\Big)" : "" ) << ", \\qquad [f, c] = ["
          << Num( rt->LocalFloor() ) << ", " << Num( rt->LocalCap() ) << "],\\; F = "
          << Num( rt->GlobalFloor() ) << " $$\n";
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

        //! the formal payoff and the realized-variance estimator
        o << "\nFormally, with $S_{t_0}, \\dots, S_{t_n}$ the scheduled observations"
          << ( vs->IsSeasoned() ? " (the past ones read from the booked fixings)" : "" )
          << ":\n\n"
          << "$$ \\text{Payoff}(T) \\;=\\; N_{\\mathrm{var}} \\times \\left( \\sigma^2_{\\mathrm{real}} - K_{\\mathrm{var}} \\right), "
             "\\qquad N_{\\mathrm{var}} = "
          << Num( vs->GetNotional() ) << ",\\; K_{\\mathrm{var}} = ("
          << Num( 100 * vs->GetVolatilityStrike() ) << "\\%)^2 $$\n\n"
          << "$$ \\sigma^2_{\\mathrm{real}} \\;=\\; \\frac{1}{T_{\\mathrm{tot}}} "
             "\\sum_{i=1}^{n} \\ln^2\\!\\frac{S_{t_i}}{S_{t_{i-1}}}"
          << ( vs->IsSeasoned()
                   ? ", \\qquad T_{\\mathrm{tot}} = " + Num( vs->GetTotalYearFraction() ) +
                         " \\text{ (from the window start)}"
                   : ", \\qquad T_{\\mathrm{tot}} = \\text{time to maturity}" )
          << " $$\n";
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

        //! the formal payoff: vanilla intrinsic times the barrier-event indicator
        const string extremum = bar->IsUp() ? "\\max" : "\\min";
        const string cmp = bar->IsUp() ? "\\ge" : "\\le";
        const string domain = bar->IsDiscrete() ? "t_i \\in \\text{monitoring dates}"
                                                : "0 \\le t \\le T";
        const string running = bar->IsDiscrete() ? extremum + "_{" + domain + "} S_{t_i}"
                                                 : extremum + "_{" + domain + "} S_t";
        const string intrinsic = call ? "\\max(S_T - K,\\, 0)" : "\\max(K - S_T,\\, 0)";
        o << "\nFormally:\n\n"
          << "$$ \\text{Payoff}(T) \\;=\\; " << intrinsic << " \\cdot \\mathbf{1}\\!\\left\\{ "
          << running << " " << cmp << " H \\right\\}"
          << ( bar->IsIn() ? "" : "^{\\complement}" ) << ", \\qquad K = " << Num( bar->_strike )
          << ",\\; H = " << Num( bar->Level() ) << " $$\n"
          << ( bar->IsIn() ? ""
                           : "\n(the indicator's complement: the option pays only if the "
                             "barrier is NEVER touched).\n" );
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

        //! the formal payoff (exercise at the holder's chosen time for an American)
        const string intrinsic = call ? "\\max(S_{%T%} - K,\\, 0)" : "\\max(K - S_{%T%},\\, 0)";
        if ( van->IsAmerican() )
        {
            string body = intrinsic;
            body.replace( body.find( "%T%" ), 3, "\\tau" );
            o << "\nFormally, exercised at the holder's chosen time $\\tau \\le T$:\n\n"
              << "$$ \\text{Payoff}(\\tau) \\;=\\; " << body << ", \\qquad K = "
              << Num( van->GetStrike() ) << " $$\n";
        }
        else
        {
            string body = intrinsic;
            body.replace( body.find( "%T%" ), 3, "T" );
            o << "\nFormally:\n\n"
              << "$$ \\text{Payoff}(T) \\;=\\; " << body << ", \\qquad K = "
              << Num( van->GetStrike() ) << " $$\n";
        }
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

    if ( auto* as = dynamic_cast<Asian*>( _contract ) )
    {
        o << "## Observation schedule\n\n"
          << "Averaging fixings (the maturity date is always the last one):\n\n";
        for ( const date& d : as->GetObservationDates() )
        {
            o << "- " << Iso( d ) << "\n";
        }
        o << "\n";
        return o.str();
    }

    if ( auto* rt = dynamic_cast<Ratchet*>( _contract ) )
    {
        o << "## Observation schedule\n\n"
          << "Period boundaries (consecutive dates bound each period return):\n\n";
        for ( const date& d : rt->GetObservationDates() )
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
