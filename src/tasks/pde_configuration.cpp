#include "thoth.hpp"
#include "pde_configuration.hpp"

//!
PdeConfiguration::PdeConfiguration( const string& ObjectName ) : Object( ObjectName, KIND_PDE_CONFIGURATION )
{
}

//!
PdeConfiguration::~PdeConfiguration() = default;
