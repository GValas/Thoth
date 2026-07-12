#include "thoth.hpp"
#include "pricer_mcl.hpp"
#include "cancellation.hpp"
#include "contract.hpp"
#include "enums.hpp"
#include "vanilla.hpp"
#include "mcl_gpu.hpp"
#include "object_reader.hpp"
#include "progress_bar.hpp"
#include "path_generator.hpp"
#include "single.hpp" //!< Volatility / IsStochastic (MonoVol helper for the GPU gate)
#include "maths.hpp"
#include <algorithm>
#include <cmath>

//! pricer_mcl_gpu_gate.cpp — the GPU gating of PricerMCL: whether the GBM
//! kernel can price the book (and each contract), and the per-contract GPU
//! pricing entry point. Split out of pricer_mcl.cpp (pure move).

namespace
{
//! the single's volatility iff the underlying is a mono (exactly one single name),
//! else null — used to reject stochastic vol from the GPU GBM gate.
Volatility* MonoVol( Underlying* u )
{
    SingleSet s = u->GetSingleSet();
    return ( s.size() == 1 ) ? ( *s.begin() )->GetVolatility() : nullptr;
}

//! forward-measure GBM scalars handed to the GPU kernel (the same conventions as the
//! analytic BS price).
struct GpuGbmParams
{
    double forward = 0; //!< carries the carry / dividend / quanto drift
    double strike = 0;
    double t = 0;   //!< year fraction today -> maturity
    double vol = 0; //!< implied vol at (strike, maturity)
    double df = 0;  //!< discount factor to maturity
    bool is_call = true;
};

//! whether the GPU GBM kernel can price this contract, and if so its scalars: a
//! genuine single-asset European vanilla under deterministic-vol GBM. An engine
//! decision read off the (pure-description) contract + underlying; American /
//! stochastic-vol (Heston) / multi-asset are rejected (CPU fallback).
bool GpuGbmParamsFor( Contract* Ctr, const date& Today, GpuGbmParams& Out )
{
    Vanilla* v = dynamic_cast<Vanilla*>( Ctr );
    if ( !v || v->IsAmerican() )
    {
        return false; //!< only European vanillas
    }
    Underlying* u = v->GetUnderlying();
    if ( !u->IsMono() )
    {
        return false; //!< composite / basket need a multi-asset kernel
    }
    if ( Volatility* mvol = MonoVol( u ); mvol && mvol->IsStochastic() )
    {
        return false; //!< stochastic vol needs the QE / CF path, not lognormal GBM
    }

    Currency* ccy = v->GetPremiumCurrency();
    if ( ccy->GetRateModel() )
    {
        return false; //!< stochastic rates: the GBM kernel's flat df/drift would misprice
    }
    const date maturity = v->GetMaturityDate();
    Out.t = YearFraction( Today, maturity );
    Out.df = ccy->GetDiscountRate()->GetDiscountFactor( maturity );
    Out.forward = u->GetForward( maturity, ccy );
    Out.vol = u->GetImplicitVol( v->GetStrike(), maturity );
    Out.strike = v->GetStrike();
    Out.is_call = ( v->GetType() == OptionType::Call );
    return true;
}
} // namespace

//! GPU-priceable iff a device is present and every contract is a GPU-supported
//! European vanilla under GBM. All-or-nothing: a mixed book runs on the CPU so the
//! result is never a half-GPU/half-CPU patchwork.
bool PricerMCL::BookIsGpuSupported()
{
    if ( !gpu::Available() )
    {
        return false;
    }
    //! GpuGbmParamsFor reads each contract's forward, and a QUANTO forward needs the
    //! correlation propagated to its underlying (spot/FX rho). This probe runs inside
    //! PreCheck, which is BEFORE PriceBook -> InitPricing -> Book::Reset does that
    //! propagation — so wire it here first (idempotent: InitPricing re-anchors the book
    //! before the actual price). Without this a quanto book erroneously errors on the GPU
    //! gate ("missing correlation for quanto adjustment") unless some earlier sequence cell
    //! happened to set the correlation on the shared underlying.
    _book->Reset( _today, _correlation );
    for ( Contract* c : _book->GetContractSet() )
    {
        GpuGbmParams p;
        if ( !GpuGbmParamsFor( c, _today, p ) )
        {
            return false;
        }
    }
    return true;
}

//! price one contract on the GPU (GBM European vanilla). Used only in _use_gpu
//! mode, by the per-contract loop and its bump-and-revalue Greeks.
void PricerMCL::PriceContract( Contract* Ctr )
{
    GpuGbmParams p;
    if ( !GpuGbmParamsFor( Ctr, _today, p ) )
    {
        //! BookIsGpuSupported guarantees every contract is supported in GPU mode
        ERR( "gpu pricing '" + _name + "': contract '" + Ctr->GetName() + "' is not GPU-supported" );
    }

    const long paths = _mcl->_paths;
    //! one fixed seed for the base price and every bump -> common random numbers
    const unsigned long seed = (unsigned long)_mcl->_seed;

    const gpu::GbmResult r = gpu::PriceEuropeanGbm( p.forward, p.strike, p.t, p.vol,
                                                    p.df, p.is_call, paths, seed );
    Result( Ctr ).premium = r.premium;
    Result( Ctr ).premium_trust = r.trust;
}
