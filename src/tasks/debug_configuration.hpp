#pragma once
#include "object.hpp"

//! Debug switches for a pricer, grouped in their own YAML object (kind
//! "debug_configuration") and referenced from a pricer via its
//! "debug_configuration" field. All switches default to off, so a pricer
//! without a debug_configuration behaves exactly as before.
class DebugConfiguration : public Object
{
  public:
    //! dump the Monte-Carlo node graph (the built node DAG) to a Graphviz .dot
    //! file next to the log path, for inspection / rendering.
    bool _generate_nodes_graph = false;

    DebugConfiguration( const string& ObjectName );
    ~DebugConfiguration() override;
};
