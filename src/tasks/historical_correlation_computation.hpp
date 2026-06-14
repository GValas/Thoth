#pragma once
#include "correlation.hpp"
#include "simple_fixing_data.hpp"
#include "task.hpp"

class HistoricalCorrelationComputation : public Task
{

  private:
    //!
    GslMatrix _historical_matrix;

    //! cfg attributes
    Correlation* _correlation;
    double _half_life = 0;
    int _time_step = 0;
    size_t _range_size = 0;
    vector<SimpleFixingData*> _historical_spots_fixing_list;

  public:
    //! setter
    void SetCorrelation( Correlation* correlation );
    void SetHalfLife( double HalfLife );
    void SetTimeStep( int TimeStep );
    void SetRangeSize( int RangeSize );
    void SetHistoricalSpotsFixingList( const vector<SimpleFixingData*>& HistoricalSpotsFixingList );

    //! constructor, destructor
    HistoricalCorrelationComputation( const string& ObjectName,
                                      YamlConfig& YamlConfig );
    ~HistoricalCorrelationComputation() override;

    //!
    void Execute() override;
    void WriteResults() override;
};
