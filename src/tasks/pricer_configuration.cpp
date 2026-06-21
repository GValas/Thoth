#include "thoth.hpp"
#include "pricer_configuration.hpp"
#include "object_reader.hpp"

//!
PricerConfiguration::PricerConfiguration( const string& ObjectName ) : Object( ObjectName, KIND_PRICER_CONFIGURATION )
{
}

//!
PricerConfiguration::~PricerConfiguration() = default;

//! read the method and the optional engine-parameter references
void PricerConfiguration::Configure( ObjectReader& reader )
{
    _method = reader.Get<string>( "method" );
    if ( reader.Has<string>( "mcl_configuration" ) )
    {
        _mcl = reader.Ref<MclConfiguration>( "mcl_configuration" );
    }
    if ( reader.Has<string>( "pde_configuration" ) )
    {
        _pde = reader.Ref<PdeConfiguration>( "pde_configuration" );
    }
    if ( reader.Has<string>( "log_path" ) )
    {
        _log_path = reader.Get<string>( "log_path" );
    }
}
