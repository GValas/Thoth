#include "thoth.hpp"
#include "pricer_configuration.hpp"

//!
PricerConfiguration::PricerConfiguration( const string& ObjectName ) : Object( ObjectName, KIND_PRICER_CONFIGURATION )
{
}

//!
PricerConfiguration::~PricerConfiguration() = default;
