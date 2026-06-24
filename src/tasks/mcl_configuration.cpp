#include "thoth.hpp"
#include "mcl_configuration.hpp"
#include "object_reader.hpp"

//! pure parameter object (kind KIND_MCL_CONFIGURATION): fields are populated later
//! by Configure, nothing to set up here
MclConfiguration::MclConfiguration( const string& ObjectName ) : Object( ObjectName, KIND_MCL_CONFIGURATION )
{
}

//! no owned resources
MclConfiguration::~MclConfiguration() = default;

//! read the engine parameters, then guard against degenerate grids
void MclConfiguration::Configure( ObjectReader& reader )
{
    _max_day_step = reader.Get<int>( "max_day_step" );
    _min_day_step = reader.Get<int>( "min_day_step" );
    _paths = reader.Get<long>( "paths" );
    //! year-fraction sub-step, so it is a double (0.01, not 0)
    _vol_year_step = reader.Get<double>( "vol_year_step" );
    _node_file = reader.Get<string>( "node_file", MCL_NODE_PATH );
    _use_sobol = reader.Get<bool>( "use_sobol", MC_USE_SOBOL );
    _seed = reader.Get<int>( "seed", 0 );
    _sobol_skip = reader.Get<long>( "sobol_skip", 0 );
    _allow_gpu = reader.Get<bool>( "allow_gpu", false );
    //! guard against degenerate grids: paths <= 1 -> the (n-1) sample-variance
    //! denominator in the MC trust / standard error is 0 -> NaN; and
    //! max_day_step <= 0 -> a zero-day diffusion step that never advances
    if ( _paths <= 1 )
    {
        ERR( "mcl_configuration '" + GetName() + "': paths must be > 1" );
    }
    if ( _max_day_step <= 0 )
    {
        ERR( "mcl_configuration '" + GetName() + "': max_day_step must be > 0 (days)" );
    }
}
