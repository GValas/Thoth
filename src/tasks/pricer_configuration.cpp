#include "thoth.hpp"
#include "pricer_configuration.hpp"
#include "object_reader.hpp"

//! pure reference holder (kind KIND_PRICER_CONFIGURATION): the method and engine
//! references are resolved later by Configure
PricerConfiguration::PricerConfiguration( const string& ObjectName ) : Object( ObjectName, KIND_PRICER_CONFIGURATION )
{
}

//! engine-config objects are owned by the ObjectManager, not here
PricerConfiguration::~PricerConfiguration() = default;

//! read the method and the optional engine-parameter references
void PricerConfiguration::Configure( ObjectReader& reader )
{
    _method = reader.Get<string>( "method" ); //!< "pde" / "mcl" / "ana" (picks the engine)
    //! engine-parameter objects are optional and resolved by reference only when
    //! present: an "ana" config needs neither, "mcl"/"pde" each point at their own.
    //! The chosen engine's PreCheck() later errors if its parameter object is null.
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
