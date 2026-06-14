#include "thoth.hpp"
#include "debug_configuration.hpp"

DebugConfiguration::DebugConfiguration( const string& ObjectName )
    : Object( ObjectName, KIND_DEBUG_CONFIGURATION )
{
}

DebugConfiguration::~DebugConfiguration() = default;
