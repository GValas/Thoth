#include "thoth.hpp"
#include "pde_configuration.hpp"
#include "object_reader.hpp"

//! pure parameter object (kind KIND_PDE_CONFIGURATION): grid sizes are filled in by
//! Configure from the precision preset, nothing to set up here
PdeConfiguration::PdeConfiguration( const string& ObjectName ) : Object( ObjectName, KIND_PDE_CONFIGURATION )
{
}

//! no owned resources
PdeConfiguration::~PdeConfiguration() = default;

//! read the vanilla precision preset (maps to default grid sizes), then apply any
//! explicit grid / sigma-factor overrides
void PdeConfiguration::Configure( ObjectReader& reader )
{
    const string prec = reader.Get<string>( "vanilla_precision", "high" );
    if ( prec == "low" )
    {
        _vanilla_precision = Precision::Low;
        _custom_n_s = PDE_VANILLA_PRECISION_LOW_N_S;
        _custom_n_t = PDE_VANILLA_PRECISION_LOW_N_T;
    }
    else if ( prec == "medium" )
    {
        _vanilla_precision = Precision::Medium;
        _custom_n_s = PDE_VANILLA_PRECISION_MEDIUM_N_S;
        _custom_n_t = PDE_VANILLA_PRECISION_MEDIUM_N_T;
    }
    else if ( prec == "high" )
    {
        _vanilla_precision = Precision::High;
        _custom_n_s = PDE_VANILLA_PRECISION_HIGH_N_S;
        _custom_n_t = PDE_VANILLA_PRECISION_HIGH_N_T;
    }
    else
    {
        ERR( "unknown vanilla_precision '" + prec + "' (expected 'low', 'medium' or 'high')" );
    }
    if ( reader.Has<int>( "custom_n_s" ) )
    {
        _custom_n_s = reader.Get<int>( "custom_n_s" );
    }
    if ( reader.Has<int>( "custom_n_t" ) )
    {
        _custom_n_t = reader.Get<int>( "custom_n_t" );
    }
    //! bound the grid sizes: they drive la_vector allocations and the O(n_s * n_t)
    //! backward sweep, so an unbounded (or negative -> huge size_t) value would OOM
    //! or hang the engine. A value outside (0, cap] is a config error, not a silent
    //! clamp — the caller sees why. (Untrusted-YAML trust boundary; see constants.hpp.)
    if ( _custom_n_s <= 0 || _custom_n_s > PDE_MAX_GRID_NODES )
    {
        ERR( "pde_configuration '" + GetName() + "': custom_n_s must be in (0, " +
             std::to_string( PDE_MAX_GRID_NODES ) + "]" );
    }
    if ( _custom_n_t <= 0 || _custom_n_t > PDE_MAX_GRID_NODES )
    {
        ERR( "pde_configuration '" + GetName() + "': custom_n_t must be in (0, " +
             std::to_string( PDE_MAX_GRID_NODES ) + "]" );
    }
    if ( reader.Has<double>( "custom_sigma_factor" ) )
    {
        _custom_sigma_factor = reader.Get<double>( "custom_sigma_factor" );
    }
    else
    {
        _custom_sigma_factor = PDE_SIGMA_FACTOR;
    }
}
