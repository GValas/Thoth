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

//! pricer_mcl_american.cpp — the American (Longstaff-Schwartz) pass of
//! PricerMCL: path recording setup, the policy fit on the base paths and its
//! frozen application to the base and Greek-scenario paths. Split out of
//! pricer_mcl.cpp (pure move).

//! total backward-induction steps the LSM fit will run across all American
//! contracts (one per interior exercise date) — used to size the progress bar so
//! the American post-pass is embedded in the same bar as the path sweep.
long PricerMCL::AmericanLsmSteps() const
{
    if ( !_recorder.IsRecording() )
    {
        return 0;
    }
    long steps = 0;
    for ( Contract* c : _book->GetContractSet() )
    {
        if ( !c->IsAmerican() )
        {
            continue;
        }
        //! FitAmericanPolicy iterates t = M-2 .. 1  ->  M-2 steps (M = grid size)
        long m = (long)_collector.DiffusionIndicesUpTo( c->GetMaturityDate() ).size();
        if ( m > 2 )
        {
            steps += m - 2;
        }
    }
    return steps;
}

//! name of the diffusion node carrying a contract's exercise value. Asking the
//! underlying for its node works for every kind (Mono -> "<name>#spot",
//! composite -> "<eq>_compo_<ccy>#spot", basket -> its own node), unlike the
//! "<underlying-name>#spot" convention which only holds for Mono.
string PricerMCL::AmericanSpotName( Contract* Contract )
{
    MonteCarloNode* spot = Contract->GetUnderlying()->GetNode( _collector );
    return spot ? spot->GetName() : "";
}

//! register the underlying spot of every American contract for path recording
void PricerMCL::SetupAmericanRecording()
{
    size_t n = (size_t)_mcl->_paths;
    for ( Contract* c : _book->GetContractSet() )
    {
        if ( !c->IsAmerican() )
        {
            continue;
        }
        //! the exercise-value node (handles composite / basket, not just Mono)
        MonteCarloNode* spot = c->GetUnderlying()->GetNode( _collector );
        if ( !spot )
        {
            continue;
        }
        const string spot_name = spot->GetName();
        vector<size_t> grid = _collector.DiffusionIndicesUpTo( c->GetMaturityDate() );

        //! base spot path : feeds the LSM policy fit and the base premium
        _recorder.StartRecording( spot, grid, n, _collector.GetDateList() );

        //! single-tree Greeks: also record each bump scenario's spot path so the
        //! frozen exercise policy can be applied to the bumped paths (the spot
        //! diffusion node is always duplicated per scenario, so every bump — spot,
        //! vol and rate — exposes its own correctly-bumped path here)
        for ( auto& tagged : _scenario_roots )
        {
            if ( MonteCarloNode* sb = _collector.GetNode( spot_name + tagged.first ) )
            {
                _recorder.StartRecording( sb, grid, n, _collector.GetDateList() );
            }
        }

        std::ostringstream oss;
        oss << "recording " << grid.size() << " exercise dates on '"
            << spot_name << "' for American contract '" << c->GetName() << "'";
        LOG( LogLabel(), oss.str() );
    }
}

//! martingale check on each recorded underlying : E[ S_T ] ~ S_0 * exp( r T )
void PricerMCL::LogRecordings()
{
    if ( !_recorder.IsRecording() )
    {
        return;
    }
    for ( Contract* c : _book->GetContractSet() )
    {
        if ( !c->IsAmerican() )
        {
            continue;
        }
        const string spot_name = AmericanSpotName( c );
        const la_matrix* paths = _recorder.RecordedPaths( spot_name );
        if ( !paths || paths->size2 == 0 )
        {
            continue;
        }
        size_t last = paths->size2 - 1;
        double mean = 0;
        for ( size_t i = 0; i < paths->size1; i++ )
        {
            mean += la_matrix_get( paths, i, last );
        }
        mean /= paths->size1;
        std::ostringstream oss;
        oss << "recorded '" << spot_name << "' paths : E[S_T] = " << mean
            << " (" << paths->size1 << " x " << paths->size2 << ")";
        LOG( LogLabel(), oss.str() );
    }
}

//! price every American contract by Longstaff-Schwartz, then re-aggregate the
//! book. Single-tree Greeks: the exercise policy is fit ONCE per contract on its
//! base paths and then applied (frozen) to the base paths AND to every bumped
//! scenario's recorded paths, replacing that contract's European contribution in
//! _scenario_premium so ComputeGreeks finite-differences the American values.
void PricerMCL::PriceAmerican()
{
    if ( !_recorder.IsRecording() )
    {
        return;
    }

    //! fx factor that converts a contract premium into the book currency
    auto fx_of = [&]( Contract* c ) -> double
    {
        if ( _currency == c->GetPremiumCurrency() )
        {
            return 1.0;
        }
        return _correlation->GetFxSpot( _currency->GetName(),
                                        c->GetPremiumCurrency()->GetName() );
    };

    for ( Contract* c : _book->GetContractSet() )
    {
        if ( !c->IsAmerican() )
        {
            continue;
        }

        const string spot_name = AmericanSpotName( c );
        const la_matrix* S0 = _recorder.RecordedPaths( spot_name );
        vector<double> tau = _recorder.RecordedTau( spot_name );
        vector<date> dates = _recorder.RecordedDates( spot_name );

        //! per-exercise-date zero rates off the premium currency's curve (parallel to
        //! tau). The diffusion drifts at the per-step forward carry of the SAME curve,
        //! so LSM discounting must follow it too: a single flat maturity-zero rate would
        //! mis-discount interior exercise cashflows by exp(-(r_T - r_t)·τ_t) on a
        //! sloped curve (the European legs, discounted by ContractNode, already follow
        //! the term structure).
        auto zero_rates_of = [&]( const vector<date>& ExerciseDates )
        {
            vector<double> rs;
            rs.reserve( ExerciseDates.size() );
            for ( const date& d : ExerciseDates )
            {
                rs.push_back( c->GetPremiumCurrency()->GetDiscountRate()->GetCurveValue( d ) );
            }
            return rs;
        };
        const vector<double> rates = zero_rates_of( dates );

        //! fit the exercise policy once on the base paths
        AmericanPolicy policy = FitAmericanPolicy( c, S0, tau, rates );

        //! base premium = apply the frozen policy to the base paths
        double trust = 0;
        double premium = ApplyAmericanPolicy( c, S0, tau, rates, policy, trust );
        Result( c ).premium = premium;
        Result( c ).premium_trust = trust;
        std::ostringstream oss;
        oss << "american '" << c->GetName() << "' (LSM, frozen boundary) premium = " << premium;
        LOG( LogLabel(), oss.str() );

        //! re-price each Greek scenario for this contract under the frozen policy,
        //! swapping the contract's European contribution for its American value in
        //! the (book-currency) scenario premium
        const double fx = fx_of( c );
        for ( auto& tagged : _scenario_roots )
        {
            const string& tag = tagged.first;
            auto it = _scenario_premium.find( tag );
            if ( it == _scenario_premium.end() )
            {
                continue;
            }

            //! European contribution of this contract under the bump (to remove)
            MonteCarloNode* euro_node = _collector.GetNode( c->GetName() + tag );
            double euro_c = euro_node ? euro_node->GetIndicatorValue( 0 ) : 0;

            //! bumped paths (spot/vol/rate). The rho scenario also discounts (and
            //! drifts, already in the path) at the bumped rates
            const la_matrix* Sb = _recorder.RecordedPaths( spot_name + tag );
            vector<double> taub = _recorder.RecordedTau( spot_name + tag );
            vector<date> datesb = _recorder.RecordedDates( spot_name + tag );
            if ( !Sb )
            {
                Sb = S0;
                taub = tau;
                datesb = dates;
            }
            vector<double> rb = zero_rates_of( datesb );
            if ( tag == scenario_tag::RHO )
            {
                //! parallel curve bump, mirroring ApplyRateShift's rho convention
                for ( double& x : rb )
                {
                    x += GREEK_RATE_BUMP;
                }
            }

            double tb = 0;
            double amer_c = ApplyAmericanPolicy( c, Sb, taub, rb, policy, tb );

            it->second += ( amer_c - euro_c ) * fx;

            //! swap the contract's European bump value for its American one in the
            //! per-contract scenario premia too, so per-contract Greeks (ComputeGreeks)
            //! finite-difference the American values just like the book aggregate above.
            _contract_scenario_premium[c->GetName()][tag] = amer_c;
        }
    }

    //! re-aggregate the base book premium from the (now American) contracts
    double book_premium = 0;
    for ( Contract* c : _book->GetContractSet() )
    {
        book_premium += Result( c ).premium * fx_of( c );
    }
    _book_result.premium = book_premium;

    //! finalise the shared bar now that the American premium is known (the sweep
    //! left it open). Shows the American book premium, not the European readback.
    if ( _progress_bar )
    {
        _progress_bar->Done( "price = " + ToString( book_premium ) );
        _progress_bar.reset();
    }
}

//! Fit a Longstaff-Schwartz exercise policy on the recorded base paths. Backward
//! induction over the exercise grid; at each interior date the continuation value
//! of the in-the-money paths is regressed on { 1, m, m^2 } (m = S/K, the moneyness
//! normalised by the STRIKE) and the per-date coefficients are stored. Normalising
//! by the strike — where the early-exercise boundary sits — keeps the regressor
//! O(1) around the decision region and better-conditions the { 1, m, m^2 } fit than
//! S/S0 would for deep in/out-of-the-money initial spots. The fitted continuation
//! also drives the cashflow roll-back so earlier dates regress against the realised
//! policy value.
PricerMCL::AmericanPolicy PricerMCL::FitAmericanPolicy( Contract* Contract,
                                                        const la_matrix* Paths,
                                                        const vector<double>& Tau,
                                                        const vector<double>& ZeroRates )
{
    AmericanPolicy pol;
    if ( !Paths || Paths->size2 < 2 )
    {
        return pol;
    }

    //! minimum in-the-money paths required before fitting the {1, m, m^2}
    //! continuation regression: with only a handful the 3-parameter fit is exactly
    //! determined (interpolation, not regression) and yields unreliable
    //! continuation values, so demand several observations per basis function.
    constexpr size_t MIN_ITM_FOR_REGRESSION = 50;

    size_t N = Paths->size1; //!< paths
    size_t M = Paths->size2; //!< columns: tau[0]=0 (today) .. tau[M-1]=maturity
    //! moneyness normaliser: the contract's own choice (a Vanilla returns its
    //! strike — the exercise boundary sits near it — anything else defaults to
    //! the base-path initial spot)
    pol.basis_norm = Contract->LsmBasisNorm( la_matrix_get( Paths, 0, 0 ) );
    pol.tau = Tau;
    pol.b0.assign( M, 0.0 );
    pol.b1.assign( M, 0.0 );
    pol.b2.assign( M, 0.0 );
    pol.has_fit.assign( M, false );

    //! cashflow per path, expressed as value at the current backward time step
    vector<double> cf( N );
    for ( size_t p = 0; p < N; p++ )
    {
        cf[p] = Contract->Intrinsic( la_matrix_get( Paths, p, M - 1 ) ); //!< at maturity
    }

    //! backward induction over the interior exercise dates
    for ( int t = (int)M - 2; t >= 1; t-- )
    {
        //! advance the shared progress bar: the LSM fit continues the same bar
        //! the path sweep started, so the whole American job fills one bar
        if ( _progress_bar )
        {
            _progress_bar->Update( ++_progress_step );
        }

        //! discount factor over [Tau[t], Tau[t+1]] as the ratio of the two zero-coupon
        //! prices, exp(r_t·τ_t - r_{t+1}·τ_{t+1}) — follows the curve's term structure
        //! (reduces to exp(-r·Δτ) on a flat curve)
        double df = exp( ZeroRates[t] * Tau[t] - ZeroRates[t + 1] * Tau[t + 1] );
        for ( size_t p = 0; p < N; p++ )
        {
            cf[p] *= df; //!< discount future cashflow to this date
        }

        //! in-the-money paths
        vector<size_t> itm;
        itm.reserve( N );
        for ( size_t p = 0; p < N; p++ )
        {
            if ( Contract->Intrinsic( la_matrix_get( Paths, p, t ) ) > 0 )
            {
                itm.push_back( p );
            }
        }
        if ( itm.size() < MIN_ITM_FOR_REGRESSION )
        {
            continue; //!< too few points for a meaningful fit -> hold at this date
        }

        //! regress discounted continuation cashflow on { 1, m, m^2 }
        size_t ni = itm.size();
        LaMatrix X = la_matrix_alloc( ni, 3 ); //!< RAII: freed on every exit (incl. throw)
        LaVector y = la_vector_alloc( ni );
        for ( size_t k = 0; k < ni; k++ )
        {
            double m = la_matrix_get( Paths, itm[k], t ) / pol.basis_norm;
            la_matrix_set( X, k, 0, 1.0 );
            la_matrix_set( X, k, 1, m );
            la_matrix_set( X, k, 2, m * m );
            la_vector_set( y, k, cf[itm[k]] );
        }
        vector<double> beta = LeastSquares( X, y );
        if ( beta.empty() )
        {
            continue; //!< singular regression at this date -> hold (has_fit stays false)
        }
        pol.b0[t] = beta[0];
        pol.b1[t] = beta[1];
        pol.b2[t] = beta[2];
        pol.has_fit[t] = true;

        //! roll the cashflow back through the fitted exercise decision
        for ( size_t p : itm )
        {
            double s = la_matrix_get( Paths, p, t );
            double m = s / pol.basis_norm;
            double continuation = pol.b0[t] + pol.b1[t] * m + pol.b2[t] * m * m;
            double intrinsic = Contract->Intrinsic( s );
            if ( intrinsic >= continuation )
            {
                cf[p] = intrinsic;
            }
        }
        //! X, y are RAII-freed at scope exit
    }

    return pol;
}

//! Apply a fitted (frozen) exercise policy to a set of paths: walk each path
//! forward and exercise at the first interior date whose intrinsic beats the
//! frozen continuation estimate, otherwise take the maturity payoff. Returns the
//! discounted MC mean (vs immediate exercise at the path-set's initial spot).
//! ZeroRates are the scenario's per-exercise-date discount zero rates (parallel to
//! Tau, curve-read, bumped for rho); the moneyness is always normalised by the
//! policy's basis_norm (the strike) so the frozen boundary stays comparable across
//! the base and bumped path sets.
double PricerMCL::ApplyAmericanPolicy( Contract* Contract,
                                       const la_matrix* Paths,
                                       const vector<double>& Tau,
                                       const vector<double>& ZeroRates,
                                       const AmericanPolicy& Policy,
                                       double& Trust )
{
    Trust = 0;
    if ( !Paths || Paths->size2 < 2 )
    {
        return 0;
    }

    size_t N = Paths->size1; //!< paths
    size_t M = Paths->size2; //!< columns: tau[0]=0 (today) .. tau[M-1]=maturity
    double sum = 0;
    double sum2 = 0;
    for ( size_t p = 0; p < N; p++ )
    {
        double value = 0; //!< discounted-to-today cashflow of this path
        for ( size_t t = 1; t < M; t++ )
        {
            double s = la_matrix_get( Paths, p, t );
            double intrinsic = Contract->Intrinsic( s );

            if ( t == M - 1 ) //!< maturity : forced exercise (take the payoff)
            {
                //! discount to today at the zero rate of THIS date (Tau[0] = 0)
                value = intrinsic * exp( -ZeroRates[t] * ( Tau[t] - Tau[0] ) );
                break;
            }

            //! interior date : exercise when intrinsic beats the frozen continuation
            if ( intrinsic > 0 && Policy.has_fit[t] )
            {
                double m = s / Policy.basis_norm;
                double continuation = Policy.b0[t] + Policy.b1[t] * m + Policy.b2[t] * m * m;
                if ( intrinsic >= continuation )
                {
                    //! discount to today at the zero rate of the EXERCISE date
                    value = intrinsic * exp( -ZeroRates[t] * ( Tau[t] - Tau[0] ) );
                    break;
                }
            }
        }
        sum += value;
        sum2 += value * value;
    }

    double mean = sum / N;
    Trust = sqrt( ( sum2 / N - mean * mean ) / ( N - 1 ) );

    //! American value = max( continuation today, immediate exercise at the
    //! path-set's initial spot ) — for bumped paths this is the bumped spot
    double s0_set = la_matrix_get( Paths, 0, 0 );
    return std::max( mean, Contract->Intrinsic( s0_set ) );
}
