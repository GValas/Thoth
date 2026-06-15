#include "thoth.hpp"
#include "historical_volatility_computation.hpp"

HistoricalVolatilityComputation::HistoricalVolatilityComputation( const string& ObjectName,
                                                                  YamlConfig& YamlConfig ) : Task( ObjectName, YamlConfig, KIND_HISTORICAL_VOLATILITY_COMPUTATION )
{
}

//!
HistoricalVolatilityComputation::~HistoricalVolatilityComputation() = default;

//!
void HistoricalVolatilityComputation::Execute()
{
    double t0 = WallClockSeconds();

    // log returns & weights
    size_t data_size = _value_list.size();
    size_t return_size = data_size - _time_step;
    double w = 1;
    double r = ( _half_life == 0 ) ? 1 : exp( -M_LN2 / _half_life );
    GslVector log_returns = gsl_vector_alloc( return_size );
    GslVector weights = gsl_vector_alloc( return_size );
    for ( size_t i = 0; i < return_size; i++ )
    {
        // log return
        double x = log( _value_list[i + _time_step] / _value_list[i] );
        gsl_vector_set( log_returns, i, x );

        // weight
        gsl_vector_set( weights, return_size - i - 1, w );
        w *= r;
    }

    //! weighted variance
    _vol = gsl_stats_wsd( gsl_vector_ptr( weights, 0 ), 1, gsl_vector_ptr( log_returns, 0 ), 1, return_size ) * sqrt( NB_OF_BUSINESS_DAYS_A_YEAR / _time_step ) * 100;

    _exec_time = ExecTime( t0 );
}

//!
void HistoricalVolatilityComputation::WriteResults()
{
    LOG( "VOL", "historical_volatility = " + ToString( _vol ) );

    //! write in cfg
    Task::WriteResults();
    _cfg->SetDouble( _result + ".historical_volatility", _vol );
}

//! setter
void HistoricalVolatilityComputation::SetHalfLife( double HalfLife )
{
    _half_life = HalfLife;
}

//! setter
void HistoricalVolatilityComputation::SetTimeStep( int TimeStep )
{
    _time_step = TimeStep;
}

//! setter
void HistoricalVolatilityComputation::SetValueList( const vector<double>& ValueList )
{
    _value_list = ValueList;
}