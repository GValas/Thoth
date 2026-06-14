#include "thoth.hpp"
#include "mcl_configuration.hpp"

//!
MclConfiguration::MclConfiguration( const string& ObjectName ) : Object( ObjectName, KIND_MCL_CONFIGURATION )
{
}

//!
MclConfiguration::~MclConfiguration() = default;
