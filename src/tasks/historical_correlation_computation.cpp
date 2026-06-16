#include "thoth.hpp"
#include "historical_correlation_computation.hpp"
#include "statistics.hpp"

HistoricalCorrelationComputation::HistoricalCorrelationComputation( const string& ObjectName,
                                                                    YamlConfig& YamlConfig ) : Task( ObjectName, YamlConfig, KIND_HISTORICAL_CORRELATION_COMPUTATION )
{
    _correlation = nullptr;
}

HistoricalCorrelationComputation::~HistoricalCorrelationComputation() = default;

//! setter
void HistoricalCorrelationComputation::SetCorrelation( Correlation* Correlation )
{
    _correlation = Correlation;
}

//! setter
void HistoricalCorrelationComputation::SetHalfLife( double HalfLife )
{
    _half_life = HalfLife;
}

//! setter
void HistoricalCorrelationComputation::SetTimeStep( int TimeStep )
{
    _time_step = TimeStep;
}

//! setter
void HistoricalCorrelationComputation::SetRangeSize( int RangeSize )
{
    _range_size = RangeSize;
}

//! setter
void HistoricalCorrelationComputation::SetHistoricalSpotsFixingList( const vector<SimpleFixingData*>& HistoricalSpotsFixingList )
{
    _historical_spots_fixing_list = HistoricalSpotsFixingList;
}

//!
void HistoricalCorrelationComputation::Execute()
{

    double t0 = WallClockSeconds();

    //! dimensions
    size_t histos_size = _historical_spots_fixing_list.size();

    //! constraints
    if ( _range_size + _time_step > _historical_spots_fixing_list[0]->GetValueList()->size )
    {
        ERR( "range_size + time_step must be smaller than size of records" );
    }

    //! log returns vectors (RAII: freed on every exit, including ERR throw)
    vector<LaVector> log_return_list;
    for ( size_t i = 0; i < histos_size; i++ )
    {
        LaVector v = la_vector_alloc( _range_size );
        for ( size_t j = 0; j < _range_size; j++ )
        {
            la_vector* w = _historical_spots_fixing_list[i]->GetValueList();
            double x = log( la_vector_get( w, w->size - _range_size + j ) /
                            la_vector_get( w, w->size - _range_size + j - _time_step ) );
            la_vector_set( v, j, x );
        }
        log_return_list.push_back( std::move( v ) );
    }

    //! weights
    LaVector weights = la_vector_alloc( _range_size );
    double r = exp( -log( 2. ) / _half_life );
    double w = 1;
    for ( size_t i = 0; i < _range_size; i++ )
    {
        la_vector_set( weights, _range_size - i - 1, w );
        w *= r;
    }

    //! compute weighted means, variances
    LaVector weighted_means = la_vector_alloc( histos_size );
    LaVector weighted_variances = la_vector_alloc( histos_size );
    for ( size_t i = 0; i < histos_size; i++ )
    {
        la_vector* v = log_return_list[i];
        double wm = WeightedMean( la_vector_ptr( weights, 0 ), la_vector_ptr( v, 0 ), _range_size );
        double wv = WeightedVarianceM( la_vector_ptr( weights, 0 ), la_vector_ptr( v, 0 ),
                                       _range_size, wm );
        la_vector_set( weighted_means, i, wm );
        la_vector_set( weighted_variances, i, wv );
    }

    //! weighted covariances
    _historical_matrix = la_matrix_alloc( histos_size, histos_size );
    for ( size_t i = 0; i < histos_size; i++ )
    {
        for ( size_t j = i + 1; j < histos_size; j++ )
        {
            double c = ext_stats_wcorrelation_m_v( la_vector_ptr( weights, 0 ), 1,
                                                       la_vector_ptr( log_return_list[i], 0 ), 1,
                                                       la_vector_ptr( log_return_list[j], 0 ), 1,
                                                       _range_size,
                                                       la_vector_get( weighted_means, i ),
                                                       la_vector_get( weighted_means, j ),
                                                       la_vector_get( weighted_variances, i ),
                                                       la_vector_get( weighted_variances, j ) );
            la_matrix_set( _historical_matrix, i, j, c );
            la_matrix_set( _historical_matrix, j, i, c );
        }
        la_matrix_set( _historical_matrix, i, i, 1 );
    }

    // ext_la_matrix_log( historical_matrix );

    //! (GSL scratch vectors above are RAII-owned and free themselves)
    _exec_time = ExecTime( t0 );
}

//!
void HistoricalCorrelationComputation::WriteResults()
{

    //! common
    Task::WriteResults();

    vector<string> underlying_list;
    vector<SimpleFixingData*>::iterator s;
    for ( s = _historical_spots_fixing_list.begin();
          s != _historical_spots_fixing_list.end();
          s++ )
    {
        underlying_list.push_back( ( *s )->GetUnderlying() );
    }
    _cfg->SetStringList( _correlation->GetName() + ".underlyings", underlying_list );
    _cfg->SetLaMatrix( _correlation->GetName() + ".matrix", _historical_matrix );
}
