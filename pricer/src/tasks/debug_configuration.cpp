#include "thoth.hpp"
#include "debug_configuration.hpp"
#include "object_reader.hpp"

//! pure switch object (kind KIND_DEBUG_CONFIGURATION); switches default off in-class
DebugConfiguration::DebugConfiguration( const string& ObjectName )
    : Object( ObjectName, KIND_DEBUG_CONFIGURATION )
{
}

//! no owned resources
DebugConfiguration::~DebugConfiguration() = default;

//! read the debug switches (all default off)
void DebugConfiguration::Configure( ObjectReader& reader )
{
    _generate_nodes_graph = reader.Get<bool>( "generate_nodes_graph", false );
}
