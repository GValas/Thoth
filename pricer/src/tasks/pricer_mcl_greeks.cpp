#include "thoth.hpp"
#include "pricer_mcl.hpp"
#include "cancellation.hpp"
#include "contract.hpp"
#include "enums.hpp"
#include "vanilla.hpp"
#include "mcl_gpu.hpp"
#include "object_reader.hpp"
#include "progress_bar.hpp"
#include "path_generator.hpp"
#include "single.hpp" //!< Volatility / IsStochastic (MonoVol helper for the GPU gate)
#include "maths.hpp"
#include <algorithm>
#include <cmath>
#include "mcl_scenario_tag.hpp"

//! pricer_mcl_greeks.cpp — the single-tree Greek machinery of PricerMCL: the
//! per-bump scenario sub-trees built into the shared collector (common random
//! numbers) and the finite-difference attribution of the book / per-contract
//! Greeks from the scenario premiums. Split out of pricer_mcl.cpp (pure move).

//! single-tree Greeks need to isolate one contract's spot bump within a shared
//! path; that requires a plain (Mono) underlying. American early exercise IS
//! supported: each scenario records its own bumped spot path and PriceAmerican
//! re-prices it under the frozen LSM policy (see PriceAmerican). Non-Mono books
//! (basket / composite) still fall back to bump-and-revalue.
bool PricerMCL::CanSingleTreeGreeks() const
{
    for ( Contract* c : _book->GetContractSet() )
    {
        if ( !c->GetUnderlying()->IsMono() )
        {
            return false;
        }
    }
    return true;
}

//! build the delta/gamma/vega/rho bump sub-trees. Each bump mutates the market,
//! builds a scenario-tagged copy of the book sub-tree (which captures the bumped
//! spot/vol/rate while reusing the shared Brownian/noise nodes), then restores
//! the market. The roots are priced together with the base tree in one sweep.
void PricerMCL::BuildGreekScenarios()
{
    //! snapshot the singles first: building a sub-tree must not invalidate the
    //! iterator, and each spot bump is relative to the unbumped spot
    const vector<Single*> singles( _single_set.begin(), _single_set.end() );

    //! BumpsRate/BumpsVol tell the market-data leaves which nodes to mutualise
    //! with the base tree (rate/vol/drift are shared unless this scenario bumps
    //! them), so only the genuinely bumped sub-tree is duplicated.
    auto build = [&]( const string& Tag, bool BumpsRate, bool BumpsVol,
                      const std::function<void()>& Mutate, const std::function<void()>& Restore )
    {
        Mutate();
        //! RAII on both the scenario mode and the market restore: an ERR thrown
        //! while building the sub-tree (a node factory rejecting the bumped
        //! market) must neither leave the collector stuck in scenario mode nor
        //! leave the market bumped for the caller's error handling.
        ScopeGuard restore_market( Restore );
        MonteCarloNode* root = nullptr;
        {
            NodeCollector::ScenarioScope scenario( _collector, Tag, BumpsRate, BumpsVol );
            root = _book->GetNode( _collector );
        }
        _scenario_roots.emplace_back( Tag, root );
    };

    //! delta (one-sided, reuses the base price) / gamma (central, needs both
    //! sides). rates and vols are unbumped (shared with the base tree).
    for ( Single* s : singles )
    {
        const double spot = s->GetSpot();
        if ( _request_delta )
        {
            const double h = GREEK_SPOT_BUMP * spot;
            build( scenario_tag::delta( s->GetName() ), false, false, [&]
                   { s->SetSpot( spot + h ); }, [&]
                   { s->SetSpot( spot ); } );
        }
        if ( _request_gamma )
        {
            const double h = GREEK_GAMMA_BUMP * spot;
            build( scenario_tag::gamma_up( s->GetName() ), false, false, [&]
                   { s->SetSpot( spot + h ); }, [&]
                   { s->SetSpot( spot ); } );
            build( scenario_tag::gamma_down( s->GetName() ), false, false, [&]
                   { s->SetSpot( spot - h ); }, [&]
                   { s->SetSpot( spot ); } );
        }
    }

    //! vega : one-sided parallel vol bump on every underlying
    if ( _request_vega )
    {
        build( scenario_tag::VEGA, false, true, [&]
               { ApplyVolShift( GREEK_VOL_BUMP ); }, [&]
               { ApplyVolShift( 0 ); } );
    }

    //! rho : one-sided parallel rate bump on every currency's curve
    if ( _request_rho )
    {
        build( scenario_tag::RHO, true, false, [&]
               { ApplyRateShift( GREEK_RATE_BUMP ); }, [&]
               { ApplyRateShift( 0 ); } );
    }
}

//! single-tree Greeks: delta/gamma/vega/rho come from the bump sub-trees priced
//! in the base path sweep (read back as _scenario_premium); theta is a separate
//! reprice (rolling today changes the diffusion-date grid). Unsupported books
//! (American / non-Mono) fall back to the bump-and-revalue base implementation.
void PricerMCL::ComputeGreeks()
{
    _per_contract_greeks_ready = false;
    if ( !_build_greek_scenarios )
    {
        Pricer::ComputeGreeks(); //!< American / non-Mono : bump-and-revalue (book-level)
        return;
    }

    const double p0 = _book_result.premium;

    //! accumulate into locals: the theta reprice below runs PriceBook -> InitPricing,
    //! which zeroes the book aggregate, so the Greeks are written into _book_result
    //! only at the very end (after that reprice).
    double delta = 0;
    double gamma = 0;
    double vega = 0;
    double rho = 0;
    double theta = 0;

    //! per-contract Greeks accumulated in lock-step with the book ones, each in the
    //! contract's own currency (so the book Greek is exactly the fx-weighted sum of
    //! these). Written into Result() only at the very end, since the theta reprice's
    //! InitPricing zeroes every contract result.
    struct Pcg
    {
        double delta = 0, gamma = 0, vega = 0, rho = 0, theta = 0;
    };
    std::map<string, Pcg> pcg;

    //! base per-contract premia captured before the theta roll overwrites them.
    std::map<string, double> base_contract;
    for ( Contract* c : _book->GetContractSet() )
    {
        base_contract[c->GetName()] = Result( c ).premium;
    }
    //! a contract's premium under bump `tag` (a bump it does not touch leaves it on the
    //! shared sub-tree, so the value is absent/equal to base -> a zero contribution).
    auto cprem = [&]( const string& name, const string& tag ) -> double
    {
        auto ci = _contract_scenario_premium.find( name );
        if ( ci != _contract_scenario_premium.end() )
        {
            auto ti = ci->second.find( tag );
            if ( ti != ci->second.end() )
            {
                return ti->second;
            }
        }
        return base_contract[name];
    };

    //! delta / gamma : summed over the per-underlying spot bumps (book + per contract)
    if ( _request_delta || _request_gamma )
    {
        for ( Single* s : _single_set )
        {
            const double spot = s->GetSpot();
            const string sn = s->GetName();
            if ( _request_delta ) //!< one-sided forward difference, reuses p0
            {
                const double h = GREEK_SPOT_BUMP * spot;
                delta += ( _scenario_premium.at( scenario_tag::delta( sn ) ) - p0 ) / h;
                for ( Contract* c : _book->GetContractSet() )
                {
                    const string& n = c->GetName();
                    pcg[n].delta += ( cprem( n, scenario_tag::delta( sn ) ) - base_contract[n] ) / h;
                }
            }
            if ( _request_gamma ) //!< central second difference (needs both sides)
            {
                const double h = GREEK_GAMMA_BUMP * spot;
                gamma += ( _scenario_premium.at( scenario_tag::gamma_up( sn ) ) - 2 * p0 +
                           _scenario_premium.at( scenario_tag::gamma_down( sn ) ) ) /
                         ( h * h );
                for ( Contract* c : _book->GetContractSet() )
                {
                    const string& n = c->GetName();
                    pcg[n].gamma += ( cprem( n, scenario_tag::gamma_up( sn ) ) - 2 * base_contract[n] +
                                      cprem( n, scenario_tag::gamma_down( sn ) ) ) /
                                    ( h * h );
                }
            }
        }
    }

    //! vega / rho : one-sided, per 1 vol point / per 1% rate move (book + per contract)
    if ( _request_vega )
    {
        vega = ( _scenario_premium.at( scenario_tag::VEGA ) - p0 ) / GREEK_VOL_BUMP * 0.01;
        for ( Contract* c : _book->GetContractSet() )
        {
            const string& n = c->GetName();
            pcg[n].vega = ( cprem( n, scenario_tag::VEGA ) - base_contract[n] ) / GREEK_VOL_BUMP * 0.01;
        }
    }
    if ( _request_rho )
    {
        rho = ( _scenario_premium.at( scenario_tag::RHO ) - p0 ) / GREEK_RATE_BUMP * 0.01;
        for ( Contract* c : _book->GetContractSet() )
        {
            const string& n = c->GetName();
            pcg[n].rho = ( cprem( n, scenario_tag::RHO ) - base_contract[n] ) / GREEK_RATE_BUMP * 0.01;
        }
    }

    //! theta : roll today one calendar day forward and reprice (base-only tree;
    //! the diffusion-date grid changes, so it cannot share the single tree). Only
    //! ONE extra graph is generated: theta reuses the already-computed base
    //! premium p0, and the base book/contract premiums are snapshotted and
    //! restored without a second reprice.
    if ( _request_theta )
    {
        const date base_today = _today;

        //! snapshot the base outputs (the roll reprice overwrites them)
        const double book_premium = _book_result.premium;
        const double book_trust = _book_result.premium_trust;
        vector<double> contract_premium;
        vector<double> contract_trust;
        for ( Contract* c : _book->GetContractSet() )
        {
            contract_premium.push_back( Result( c ).premium );
            contract_trust.push_back( Result( c ).premium_trust );
        }

        _quiet_pricing = true; //!< suppress the status lines / node-graph dump...
        _theta_pass = true;    //!< ...but still show a labelled "<engine> theta" bar
        _suppress_scenarios = true;
        _today = base_today + days( 1 );
        _graph_tree_tag = "theta"; //!< capture the rolled tree's node graph
        PriceBook();               //!< the single extra graph
        _graph_tree_tag.clear();
        const double p1 = _book_result.premium;

        //! per-contract theta = rolled premium - base premium (both contract currency),
        //! read off the just-repriced contract results before they are restored below
        for ( Contract* c : _book->GetContractSet() )
        {
            const string& n = c->GetName();
            pcg[n].theta = Result( c ).premium - base_contract[n];
        }

        _today = base_today;
        _suppress_scenarios = false;
        _theta_pass = false;
        _quiet_pricing = false;

        //! restore the base scenario (dates + premiums) without repricing
        _book->SetToday( base_today );
        _currency->SetToday( base_today );
        _book_result.premium = book_premium;
        _book_result.premium_trust = book_trust;
        size_t i = 0;
        for ( Contract* c : _book->GetContractSet() )
        {
            Result( c ).premium = contract_premium[i];
            Result( c ).premium_trust = contract_trust[i];
            i++;
        }

        theta = p1 - p0;
    }

    //! write the book Greeks now — after theta's reprice (which zeroed the aggregate)
    //! and its premium/trust restore — so none of them get wiped.
    _book_result.delta = delta;
    _book_result.gamma = gamma;
    _book_result.vega = vega;
    _book_result.rho = rho;
    _book_result.theta = theta;

    //! publish the per-contract Greeks (the theta reprice has finished, so the contract
    //! results are stable) and flag them ready so GreeksPerContract() lets WriteResults
    //! emit the <contract>_<greek> fields for the MCL engine too.
    for ( Contract* c : _book->GetContractSet() )
    {
        const Pcg& g = pcg[c->GetName()];
        Result( c ).delta = g.delta;
        Result( c ).gamma = g.gamma;
        Result( c ).vega = g.vega;
        Result( c ).rho = g.rho;
        Result( c ).theta = g.theta;
    }
    _per_contract_greeks_ready = true;
}
