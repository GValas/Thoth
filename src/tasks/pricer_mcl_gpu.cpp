#include "thoth.hpp"
#include "pricer_mcl_gpu.hpp"
#include "mcl_gpu.hpp"
#include "contract.hpp"
#include <cmath>

PricerMCLGpu::PricerMCLGpu( const string& ObjectName,
                            YamlConfig& YamlConfig )
    : PricerMCL( ObjectName, YamlConfig )
{
}

PricerMCLGpu::~PricerMCLGpu() = default;

//! GPU-priceable iff a device is present and every contract is a GPU-supported
//! European vanilla under GBM. All-or-nothing: a mixed book falls back to CPU MCL
//! so the result is never a half-GPU/half-CPU patchwork.
bool PricerMCLGpu::BookIsGpuSupported()
{
    if ( !gpu::Available() )
    {
        return false;
    }
    for ( Contract* c : _book->GetOptionList() )
    {
        GpuGbmParams p;
        if ( !c->GPU_GbmParams( p ) )
        {
            return false;
        }
    }
    return true;
}

void PricerMCLGpu::PreCheck()
{
    PricerMCL::PreCheck(); //!< same mcl_configuration + correlation requirements

    _use_gpu = BookIsGpuSupported();
    if ( _use_gpu )
    {
        LOG( "GPU", "device Monte-Carlo enabled: " + gpu::DeviceInfo() );
    }
    else
    {
        LOG( "GPU", "GPU pricing unavailable or unsupported for this book — falling back "
                    "to the CPU MCL engine (" +
                        gpu::DeviceInfo() + ")" );
    }
}

bool PricerMCLGpu::GreeksPerContract() const
{
    //! GPU mode prices contract by contract (per-contract bump Greeks); the CPU
    //! fallback keeps MCL's book-level single-tree Greeks.
    return _use_gpu;
}

void PricerMCLGpu::PriceBook()
{
    if ( !_use_gpu )
    {
        PricerMCL::PriceBook(); //!< CPU fallback (book-level diffusion tree)
        return;
    }

    //! per-contract loop (+ per-contract Greeks when requested), reusing the same
    //! machinery as the PDE / ANA engines.
    PriceBookByContract( "GPU" );

    //! AggregateContract sums premia but not the Monte-Carlo trust; for independent
    //! contracts the book MC error adds in quadrature (FX-scaled to book currency).
    double var = 0;
    for ( Contract* c : _book->GetOptionList() )
    {
        const double t = c->GetPremiumTrust() * FxToBook( c );
        var += t * t;
    }
    _book->SetPremiumTrust( std::sqrt( var ) );
}

void PricerMCLGpu::PriceContract( Contract* Ctr )
{
    GpuGbmParams p;
    if ( !Ctr->GPU_GbmParams( p ) )
    {
        //! BookIsGpuSupported guarantees every contract is supported in GPU mode
        ERR( "gpu pricing '" + _name + "': contract '" + Ctr->GetName() + "' is not GPU-supported" );
    }

    const long paths = _configuration->_mcl->_paths;
    //! one fixed seed for the base price and every bump -> common random numbers
    const unsigned long seed = (unsigned long)_configuration->_mcl->_seed;

    const gpu::GbmResult r = gpu::PriceEuropeanGbm( p.forward, p.strike, p.t, p.vol,
                                                    p.df, p.is_call, paths, seed );
    Ctr->SetPremium( r.premium );
    Ctr->SetPremiumTrust( r.trust );
}
