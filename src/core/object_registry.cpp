#include "thoth.hpp"
#include "object_manager.hpp"

//! The single translation unit aware of every concrete object type. The kind
//! tag -> factory table below is the one place to touch when adding a type:
//! one entry here plus the new class, the ObjectManager stays type-agnostic.

#include "enums.hpp"

//! tasks
#include "pricer.hpp"
#include "pricer_ana.hpp"
#include "pricer_mcl.hpp"
#include "pricer_pde.hpp"
#include "pricer_configuration.hpp"
#include "debug_configuration.hpp"
#include "sequence.hpp"
#include "historical_volatility_computation.hpp"
#include "historical_correlation_computation.hpp"

//! instruments
#include "book.hpp"
#include "vanilla.hpp"
#include "barrier.hpp"
#include "variance_swap.hpp"

//! underlyings
#include "equity.hpp"
#include "mono.hpp"
#include "absolute_basket.hpp"
#include "composite.hpp"
#include "forex.hpp"

//! market data
#include "currency.hpp"
#include "yield_curve.hpp"
#include "repo_curve.hpp"
#include "continuous_dividends_curve.hpp"
#include "bs_volatility.hpp"
#include "sabr_volatility.hpp"
#include "correlation.hpp"
#include "simple_fixing_data.hpp"

namespace
{
//! attributes common to every contract (resolved after the concrete part)
void ConfigureContractCommon( ObjectManager& m, Contract* c, const string& n )
{
    c->SetUnderlying( *m.Get<Underlying>( m.cfg().GetString( n + ".underlying" ) ) );
    c->SetPremiumCurrency( *m.Get<Currency>( m.cfg().GetString( n + ".premium_currency" ) ) );

    //! force underlying currency for a basket
    if ( c->GetUnderlying()->GetKind() == KIND_BASKET )
    {
        c->GetUnderlying()->SetCurrency( *c->GetPremiumCurrency() );
    }
}

//! optional calendar shared by every volatility
void ConfigureVolatilityCommon( ObjectManager& m, Volatility* v, const string& n )
{
    if ( m.cfg().IsString( n + ".calendar" ) )
    {
        const string cal = m.cfg().GetString( n + ".calendar" );
        v->SetNonWorkingDaysWeight( m.cfg().GetDouble( cal + ".non_working_days_weight" ) );
    }
}

//! build the kind -> factory table (one entry per object type)
map<string, ObjectManager::Factory> MakeRegistry()
{
    map<string, ObjectManager::Factory> r;

    // ---- tasks --------------------------------------------------------
    r[KIND_PRICER] = []( ObjectManager& m, const string& n ) -> Object*
    {
        //! the configuration drives the concrete pricer type
        PricerConfiguration* PC = m.Get<PricerConfiguration>( m.cfg().GetString( n + ".configuration" ) );
        std::unique_ptr<Pricer> p;
        if ( PC->_method == PRICING_METHOD_MCL )
        {
            p = std::make_unique<PricerMCL>( n, m.cfg() );
        }
        else if ( PC->_method == PRICING_METHOD_PDE )
        {
            p = std::make_unique<PricerPDE>( n, m.cfg() );
        }
        else if ( PC->_method == PRICING_METHOD_ANA )
        {
            p = std::make_unique<PricerANA>( n, m.cfg() );
        }
        else
        {
            ERR( "pricing configuration '" + PC->GetName() + "' has unknown method '" +
                 PC->_method + "' (expected '" + PRICING_METHOD_PDE + "', '" +
                 PRICING_METHOD_MCL + "' or '" + PRICING_METHOD_ANA + "')" );
        }
        Pricer* B = m.collector().Add( std::move( p ) );
        B->SetCurrency( *m.Get<Currency>( m.cfg().GetString( n + ".currency" ) ) );
        B->SetBook( *m.Get<Book>( m.cfg().GetString( n + ".book" ) ) );
        B->SetToday( m.cfg().GetDate( n + ".today" ) );
        B->SetConfiguration( *PC );
        B->SetIndicatorRequestList( m.cfg().GetStringList( n + ".indicators" ) );
        B->SetResult( m.cfg().GetString( n + ".result" ) );
        if ( m.cfg().IsString( n + ".correlation" ) )
        {
            B->SetCorrelation( m.Get<Correlation>( m.cfg().GetString( n + ".correlation" ) ) );
        }
        if ( m.cfg().IsString( n + ".debug_configuration" ) )
        {
            B->SetDebugConfiguration( m.Get<DebugConfiguration>( m.cfg().GetString( n + ".debug_configuration" ) ) );
        }
        return B;
    };

    r[KIND_SEQUENCE] = []( ObjectManager& m, const string& n ) -> Object*
    {
        Sequence* S = m.collector().Add( std::make_unique<Sequence>( n, m.cfg() ) );
        //! resolve each referenced task (built and configured on demand). Their
        //! own factories have already set each task's result block.
        const vector<string> names = m.cfg().GetStringList( n + ".tasks" );
        S->SetTaskList( m.GetList<Task>( names ), names );
        S->SetResult( m.cfg().GetString( n + ".result", "" ) );
        return S;
    };

    r[KIND_HISTORICAL_VOLATILITY_COMPUTATION] = []( ObjectManager& m, const string& n ) -> Object*
    {
        HistoricalVolatilityComputation* H =
            m.collector().Add( std::make_unique<HistoricalVolatilityComputation>( n, m.cfg() ) );
        H->SetHalfLife( m.cfg().GetDouble( n + ".half_life" ) );
        H->SetTimeStep( m.cfg().GetInteger( n + ".time_step" ) );
        H->SetValueList( m.cfg().GetDoubleList( n + ".values" ) );
        H->SetResult( m.cfg().GetString( n + ".result" ) );
        return H;
    };

    r[KIND_HISTORICAL_CORRELATION_COMPUTATION] = []( ObjectManager& m, const string& n ) -> Object*
    {
        HistoricalCorrelationComputation* H =
            m.collector().Add( std::make_unique<HistoricalCorrelationComputation>( n, m.cfg() ) );
        H->SetHalfLife( m.cfg().GetDouble( n + ".half_life" ) );
        H->SetTimeStep( m.cfg().GetInteger( n + ".time_step" ) );
        H->SetRangeSize( m.cfg().GetInteger( n + ".range_size" ) );
        H->SetCorrelation( m.Get<Correlation>( m.cfg().GetString( n + ".correlation" ) ) );
        H->SetHistoricalSpotsFixingList( m.GetList<SimpleFixingData>( m.cfg().GetStringList( n + ".historical_spots_fixings" ) ) );
        H->SetResult( m.cfg().GetString( n + ".result" ) );
        return H;
    };

    // ---- instruments --------------------------------------------------
    r[KIND_VANILLA] = []( ObjectManager& m, const string& n ) -> Object*
    {
        Vanilla* V = m.collector().Add( std::make_unique<Vanilla>( n ) );
        V->SetStrike( m.cfg().GetDouble( n + ".strike" ) );
        V->SetExerciseMode( ParseExerciseMode( m.cfg().GetString( n + ".exercise" ) ) );
        V->SetMaturityDate( m.cfg().GetDate( n + ".maturity" ) );
        V->SetType( ParseOptionType( m.cfg().GetString( n + ".type" ) ) );
        ConfigureContractCommon( m, V, n );
        return V;
    };

    r[KIND_BARRIER] = []( ObjectManager& m, const string& n ) -> Object*
    {
        Barrier* B = m.collector().Add( std::make_unique<Barrier>( n ) );
        B->_strike = m.cfg().GetDouble( n + ".strike" );
        B->_maturity_date = m.cfg().GetDate( n + ".maturity" );
        B->_type = ParseOptionType( m.cfg().GetString( n + ".type" ) );
        B->_barrier_type = ParseBarrierType( m.cfg().GetString( n + ".barrier_type" ) );
        B->_barrier_monitoring_type = ParseBarrierMonitoring( m.cfg().GetString( n + ".barrier_monitoring_type" ) );
        B->_monitoring_period_days = m.cfg().GetInteger( n + ".monitoring_period_days", 0 );
        B->_barrier_up_level = m.cfg().GetDouble( n + ".barrier_up_level", 0 );
        B->_barrier_down_level = m.cfg().GetDouble( n + ".barrier_down_level", 0 );
        ConfigureContractCommon( m, B, n );
        return B;
    };

    r[KIND_VARIANCE_SWAP] = []( ObjectManager& m, const string& n ) -> Object*
    {
        VarianceSwap* V = m.collector().Add( std::make_unique<VarianceSwap>( n ) );
        V->SetMaturityDate( m.cfg().GetDate( n + ".maturity" ) );
        //! volatility_strike is in percent (like every vol), stored as decimal
        V->SetVolatilityStrike( m.cfg().GetDouble( n + ".volatility_strike" ) / 100.0 );
        V->SetNotional( m.cfg().GetDouble( n + ".notional", 1 ) );
        ConfigureContractCommon( m, V, n );
        return V;
    };

    // ---- underlyings --------------------------------------------------
    r[KIND_EQUITY] = []( ObjectManager& m, const string& n ) -> Object*
    {
        //! build the equity (indexed under n) ...
        Equity* E = m.collector().Add( std::make_unique<Equity>( n ) );
        E->SetSpot( m.cfg().GetDouble( n + ".spot" ) );
        E->SetVolatility( *m.Get<Volatility>( m.cfg().GetString( n + ".volatility" ) ) );
        E->SetCurrency( *m.Get<Currency>( m.cfg().GetString( n + ".currency" ) ) );
        if ( m.cfg().IsString( n + ".continuous_dividends" ) )
        {
            E->SetContinuousDividends( m.Get<ContinuousDividendsCurve>( m.cfg().GetString( n + ".continuous_dividends" ) ) );
        }
        if ( m.cfg().IsString( n + ".repo" ) )
        {
            E->SetRepo( m.Get<RepoCurve>( m.cfg().GetString( n + ".repo" ) ) );
        }
        //! ... and expose it as an underlying through a mono wrapper (not
        //! indexed: it intentionally shares the equity's name)
        auto mono = std::make_unique<Mono>( n, KIND_EQUITY );
        mono->SetSingle( *E );
        return m.collector().Own( std::move( mono ) );
    };

    r[KIND_BASKET] = []( ObjectManager& m, const string& n ) -> Object*
    {
        AbsoluteBasket* B = m.collector().Add( std::make_unique<AbsoluteBasket>( n ) );
        B->SetUnderlyingList( m.GetList<Underlying>( m.cfg().GetStringList( n + ".underlyings" ) ) );
        B->SetWeightList( m.cfg().GetGslVector( n + ".weights" ) );
        return B;
    };

    r[KIND_COMPOSITE] = []( ObjectManager& m, const string& n ) -> Object*
    {
        Composite* E = m.collector().Add( std::make_unique<Composite>( n ) );
        E->SetUnderlying( *m.Get<Underlying>( m.cfg().GetString( n + ".equity" ) ) );
        E->SetCompoCurrency( *m.Get<Currency>( m.cfg().GetString( n + ".composite_currency" ) ) );
        return E;
    };

    r[KIND_FOREX] = []( ObjectManager& m, const string& n ) -> Object*
    {
        Forex* E = m.collector().Add( std::make_unique<Forex>( n ) );
        E->SetBaseCurrency( *m.Get<Currency>( m.cfg().GetString( n + ".base_currency" ) ) );
        E->SetUnderlyingCurrency( *m.Get<Currency>( m.cfg().GetString( n + ".underlying_currency" ) ) );
        if ( m.cfg().IsString( n + ".volatility" ) )
        {
            E->SetVolatility( *m.Get<Volatility>( m.cfg().GetString( n + ".volatility" ) ) );
        }
        if ( m.cfg().IsDouble( n + ".spot" ) )
        {
            E->SetSpot( m.cfg().GetDouble( n + ".spot" ) );
        }
        return E;
    };

    // ---- market data --------------------------------------------------
    r[KIND_CURRENCY] = []( ObjectManager& m, const string& n ) -> Object*
    {
        Currency* C = m.collector().Add( std::make_unique<Currency>( n ) );
        C->SetRate( *m.Get<YieldCurve>( m.cfg().GetString( n + ".rate" ) ) );
        return C;
    };

    r[KIND_YIELD_CURVE] = []( ObjectManager& m, const string& n ) -> Object*
    {
        YieldCurve* Y = m.collector().Add( std::make_unique<YieldCurve>( n ) );
        Y->SetDateList( m.cfg().GetDateList( n + ".dates" ) );
        Y->SetValueList( m.cfg().GetGslVector( n + ".values" ) );
        return Y;
    };

    r[KIND_REPO_CURVE] = []( ObjectManager& m, const string& n ) -> Object*
    {
        RepoCurve* Y = m.collector().Add( std::make_unique<RepoCurve>( n ) );
        Y->SetDateList( m.cfg().GetDateList( n + ".dates" ) );
        Y->SetValueList( m.cfg().GetGslVector( n + ".values" ) );
        return Y;
    };

    r[KIND_CONTINUOUS_DIVIDENDS_CURVE] = []( ObjectManager& m, const string& n ) -> Object*
    {
        ContinuousDividendsCurve* Y = m.collector().Add( std::make_unique<ContinuousDividendsCurve>( n ) );
        Y->SetDateList( m.cfg().GetDateList( n + ".dates" ) );
        Y->SetValueList( m.cfg().GetGslVector( n + ".values" ) );
        return Y;
    };

    r[KIND_BS_VOLATILITY] = []( ObjectManager& m, const string& n ) -> Object*
    {
        BsVolatility* B = m.collector().Add( std::make_unique<BsVolatility>( n ) );
        B->SetVolatility( m.cfg().GetDouble( n + ".volatility" ) );
        ConfigureVolatilityCommon( m, B, n );
        return B;
    };

    r[KIND_SABR_VOLATILITY] = []( ObjectManager& m, const string& n ) -> Object*
    {
        SabrVolatility* S = m.collector().Add( std::make_unique<SabrVolatility>( n ) );
        S->SetSpot( m.cfg().GetDouble( n + ".spot" ) );
        S->SetMaturityList( m.cfg().GetDoubleList( n + ".maturities" ) );
        S->SetAlphaList( m.cfg().GetDoubleList( n + ".alpha" ) );
        S->SetBetaList( m.cfg().GetDoubleList( n + ".beta" ) );
        S->SetRhoList( m.cfg().GetDoubleList( n + ".rho" ) );
        S->SetNuList( m.cfg().GetDoubleList( n + ".nu" ) );
        ConfigureVolatilityCommon( m, S, n );
        return S;
    };

    r[KIND_CORRELATION_MATRIX] = []( ObjectManager& m, const string& n ) -> Object*
    {
        Correlation* C = m.collector().Add( std::make_unique<Correlation>( n ) );
        if ( m.cfg().IsDoubleList( n + ".matrix" ) )
        {
            C->SetMatrix( m.cfg().GetGslVector( n + ".matrix" ) );
        }
        else if ( m.cfg().IsDoubleList( n + ".symmetric_matrix" ) )
        {
            C->SetSymmetricMatrix( m.cfg().GetGslVector( n + ".symmetric_matrix" ) );
        }
        else
        {
            ERR( ".matrix & .symmetric matrix are missing" );
        }
        if ( m.cfg().IsStringList( n + ".underlyings" ) )
        {
            C->SetUnderlyingList( m.cfg().GetStringList( n + ".underlyings" ) );
        }
        if ( m.cfg().IsStringList( n + ".forexs" ) )
        {
            C->SetForexList( m.GetList<Forex>( m.cfg().GetStringList( n + ".forexs" ) ) );
        }
        return C;
    };

    // ---- configurations ----------------------------------------------
    r[KIND_PRICER_CONFIGURATION] = []( ObjectManager& m, const string& n ) -> Object*
    {
        PricerConfiguration* P = m.collector().Add( std::make_unique<PricerConfiguration>( n ) );
        P->_method = m.cfg().GetString( n + ".method" );
        if ( m.cfg().IsString( n + ".mcl_configuration" ) )
        {
            P->_mcl = m.Get<MclConfiguration>( m.cfg().GetString( n + ".mcl_configuration" ) );
        }
        if ( m.cfg().IsString( n + ".pde_configuration" ) )
        {
            P->_pde = m.Get<PdeConfiguration>( m.cfg().GetString( n + ".pde_configuration" ) );
        }
        if ( m.cfg().IsString( n + ".log_path" ) )
        {
            P->_log_path = m.cfg().GetString( n + ".log_path" );
        }
        return P;
    };

    r[KIND_DEBUG_CONFIGURATION] = []( ObjectManager& m, const string& n ) -> Object*
    {
        DebugConfiguration* D = m.collector().Add( std::make_unique<DebugConfiguration>( n ) );
        D->_generate_nodes_graph = m.cfg().GetBoolean( n + ".generate_nodes_graph", false );
        return D;
    };

    r[KIND_PDE_CONFIGURATION] = []( ObjectManager& m, const string& n ) -> Object*
    {
        PdeConfiguration* P = m.collector().Add( std::make_unique<PdeConfiguration>( n ) );
        const string prec = m.cfg().GetString( n + ".vanilla_precision", "high" );
        if ( prec == "low" )
        {
            P->_vanilla_precision = Precision::Low;
            P->_custom_n_s = PDE_VANILLA_PRECISION_LOW_N_S;
            P->_custom_n_t = PDE_VANILLA_PRECISION_LOW_N_T;
        }
        else if ( prec == "medium" )
        {
            P->_vanilla_precision = Precision::Medium;
            P->_custom_n_s = PDE_VANILLA_PRECISION_MEDIUM_N_S;
            P->_custom_n_t = PDE_VANILLA_PRECISION_MEDIUM_N_T;
        }
        else if ( prec == "high" )
        {
            P->_vanilla_precision = Precision::High;
            P->_custom_n_s = PDE_VANILLA_PRECISION_HIGH_N_S;
            P->_custom_n_t = PDE_VANILLA_PRECISION_HIGH_N_T;
        }
        else
        {
            ERR( "unknown vanilla_precision '" + prec + "' (expected 'low', 'medium' or 'high')" );
        }
        if ( m.cfg().IsInteger( n + ".custom_n_s" ) )
        {
            P->_custom_n_s = m.cfg().GetInteger( n + ".custom_n_s" );
        }
        if ( m.cfg().IsInteger( n + ".custom_n_t" ) )
        {
            P->_custom_n_t = m.cfg().GetInteger( n + ".custom_n_t" );
        }
        if ( m.cfg().IsDouble( n + ".custom_sigma_factor" ) )
        {
            P->_custom_sigma_factor = m.cfg().GetDouble( n + ".custom_sigma_factor" );
        }
        else
        {
            P->_custom_sigma_factor = PDE_SIGMA_FACTOR;
        }
        return P;
    };

    r[KIND_MCL_CONFIGURATION] = []( ObjectManager& m, const string& n ) -> Object*
    {
        MclConfiguration* M = m.collector().Add( std::make_unique<MclConfiguration>( n ) );
        M->_max_time_step = m.cfg().GetInteger( n + ".max_time_step" );
        M->_min_time_step = m.cfg().GetInteger( n + ".min_time_step" );
        M->_paths = m.cfg().GetInteger( n + ".paths" );
        M->_vol_time_step = m.cfg().GetInteger( n + ".vol_time_step" );
        M->_node_file = m.cfg().GetString( n + ".node_file", MCL_NODE_PATH );
        M->_use_sobol = m.cfg().GetBoolean( n + ".use_sobol", MC_USE_SOBOL );
        M->_use_milstein = m.cfg().GetBoolean( n + ".use_milstein", MC_USE_MILSTEIN );
        M->_seed = m.cfg().GetInteger( n + ".seed", 0 );
        return M;
    };

    // ---- book & fixings ----------------------------------------------
    r[KIND_BOOK] = []( ObjectManager& m, const string& n ) -> Object*
    {
        Book* B = m.collector().Add( std::make_unique<Book>( n ) );
        B->SetOptionList( m.GetList<Contract>( m.cfg().GetStringList( n + ".options" ) ) );
        return B;
    };

    r[KIND_SIMPLE_FIXING_DATA] = []( ObjectManager& m, const string& n ) -> Object*
    {
        SimpleFixingData* S = m.collector().Add( std::make_unique<SimpleFixingData>( n ) );
        S->SetDateList( m.cfg().GetDateList( n + ".dates" ) );
        S->SetValueList( m.cfg().GetGslVector( n + ".values" ) );
        S->SetUnderlying( m.cfg().GetString( n + ".underlying" ) );
        return S;
    };

    return r;
}
} // namespace

//! dispatch a name's kind tag to its factory (single registry, built once)
Object* ObjectManager::Build( const string& ObjectName )
{
    static const map<string, Factory> registry = MakeRegistry();

    const string kind = _c->GetTag( ObjectName );
    auto entry = registry.find( kind );
    if ( entry == registry.end() )
    {
        ERR( "unknown kind : " + kind );
    }
    return entry->second( *this, ObjectName );
}
