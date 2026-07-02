#include "thoth.hpp"
#include "path_recorder.hpp"

//! register a node to snapshot at the given dates; allocate the path matrix.
//! Idempotent per node so several contracts sharing an underlying record it once.
void PathRecorder::StartRecording( MonteCarloNode* Node,
                                   const vector<size_t>& DateIndices,
                                   size_t NbDraws,
                                   const vector<date>& DateList )
{
    //! already recorded ? (e.g. two American contracts on the same underlying)
    for ( const auto& r : _records )
    {
        if ( r.node == Node )
        {
            return;
        }
    }
    PathRecord record;
    record.node = Node;
    record.date_index = DateIndices;
    //! precompute each column's year fraction from today — the LSM regression works
    //! in τ, so cache it now rather than reconverting dates on every path.
    for ( size_t idx : DateIndices )
    {
        record.tau.push_back( YearFraction( DateList[0], DateList[idx] ) );
        record.dates.push_back( DateList[idx] ); //!< kept for per-date curve reads (LSM discount)
    }
    //! [ nb_draws x nb_exercise_dates ] : one row per path, filled by RecordPath
    record.paths = la_matrix_alloc( NbDraws, DateIndices.size() );
    _records.push_back( std::move( record ) );
}

//! snapshot the current draw into the next row of each recorded matrix.
//! Called once per path after PriceNodes, while the node values for this draw are live.
void PathRecorder::RecordPath()
{
    for ( auto& r : _records )
    {
        //! defensive: never write past the pre-sized matrix if called extra times
        if ( r.row >= r.paths->size1 )
        {
            continue;
        }
        //! copy each recorded date's value into this draw's row
        for ( size_t c = 0; c < r.date_index.size(); c++ )
        {
            la_matrix_set( r.paths, r.row, c, r.node->GetValue( r.date_index[c] ) );
        }
        r.row++; //!< advance to the next draw's row
    }
}

//! recorded [ nb_draws x nb_exercise_dates ] matrix for a node, or nullptr.
const la_matrix* PathRecorder::RecordedPaths( const string& NodeName ) const
{
    for ( const auto& r : _records )
    {
        if ( r.node->GetName() == NodeName )
        {
            return r.paths.get();
        }
    }
    return nullptr;
}

//! year fractions of the recorded columns for a node (the τ grid for the LSM
//! regression); empty vector if the node was not recorded.
vector<double> PathRecorder::RecordedTau( const string& NodeName ) const
{
    for ( const auto& r : _records )
    {
        if ( r.node->GetName() == NodeName )
        {
            return r.tau;
        }
    }
    return {};
}

//! calendar dates of the recorded columns for a node; empty if the node was not
//! recorded. Parallel to RecordedTau — used to read per-date zero rates off the curve.
vector<date> PathRecorder::RecordedDates( const string& NodeName ) const
{
    for ( const auto& r : _records )
    {
        if ( r.node->GetName() == NodeName )
        {
            return r.dates;
        }
    }
    return {};
}

//! (node, date-index) pairs the scheduler must force-evaluate so every recorded
//! column is computed (see the header for why a derived spot needs this).
vector<std::pair<MonteCarloNode*, size_t>> PathRecorder::SchedulePoints() const
{
    vector<std::pair<MonteCarloNode*, size_t>> points;
    for ( const auto& r : _records )
    {
        for ( size_t idx : r.date_index )
        {
            points.push_back( { r.node, idx } );
        }
    }
    return points;
}
