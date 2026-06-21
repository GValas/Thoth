#include "thoth.hpp"
#include "debug_configuration.hpp"
#include "object_reader.hpp"

DebugConfiguration::DebugConfiguration( const string& ObjectName )
    : Object( ObjectName, KIND_DEBUG_CONFIGURATION )
{
}

DebugConfiguration::~DebugConfiguration() = default;

//! read the debug switches (all default off)
void DebugConfiguration::Configure( ObjectReader& reader )
{
    _generate_nodes_graph = reader.Get<bool>( "generate_nodes_graph", false );
}
