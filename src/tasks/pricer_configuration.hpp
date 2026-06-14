#pragma once
#include "mcl_configuration.hpp"
#include "object.hpp"
#include "pde_configuration.hpp"

class PricerConfiguration : public Object
{
  private:
  public:
    string _method; //!< "pde", "mcl" or "ana"

    //! engine parameters, each grouped in its own referenced object
    MclConfiguration* _mcl = nullptr; //!< kind "mcl_configuration"
    PdeConfiguration* _pde = nullptr; //!< kind "pde_configuration"

    string _log_path;

    //!
    PricerConfiguration( const string& ObjectName );
    ~PricerConfiguration() override;
};
