#pragma once
#include "task.hpp"

class HistoricalVolatilityComputation : public Task
{
  private:
    //! attributes
    double _half_life = 0;
    int _time_step = 0;
    vector<double> _value_list;

    //! result
    double _vol;

  public:
    //! setter
    void SetHalfLife( double HalfLife );
    void SetTimeStep( int TimeStep );
    void SetValueList( const vector<double>& ValueList );

    //! constructor, destructor
    HistoricalVolatilityComputation( const string& ObjectName,
                                     YamlConfig& YamlConfig );
    ~HistoricalVolatilityComputation() override;

    //!
    void Execute() override;
    void WriteResults() override;
};
