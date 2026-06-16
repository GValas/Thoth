# GPU (RTX / CUDA) Monte-Carlo — feasibility & proposal

**Question:** can the MCL Monte-Carlo be distributed over an RTX GPU?

**Short answer:** yes — Monte-Carlo path simulation is embarrassingly parallel
and an excellent GPU fit — but *not* by reusing the current engine as-is. The RTX
gives data-parallelism *inside one machine* (thousands of CUDA cores each running
paths), which is a different axis from the existing cluster (paths split across
*processes/hosts*). The two compose: a GPU slave inside the cluster.

## Implementation status

**Increment 1 (done).** A dedicated `mcl_gpu` engine is wired in end-to-end:
- `gpu::PriceEuropeanGbm` CUDA kernel (`src/tasks/mcl_gpu.cu`): one thread per
  path, cuRAND Philox normals, forward-measure lognormal terminal, shared-memory
  block reduction of the payoff sum / sum-of-squares → premium + MC trust.
- `Contract::GPU_GbmParams` (overridden in `Vanilla`) exposes the forward-measure
  scalars for a **single-asset European vanilla under GBM** (the same scalars the
  analytic BS pricer uses); anything else returns false.
- `PricerMCLGpu` (method `mcl_gpu`) prices such books contract-by-contract on the
  device, with per-contract bump-and-revalue Greeks under a **fixed kernel seed**
  (common random numbers → smooth Greeks). Any unsupported book, a CPU-only build,
  or a host with no GPU **falls back to the CPU `mcl` engine**.
- Build: `-DTHOTH_ENABLE_CUDA=ON` (off by default). The single `Dockerfile`
  builds the GPU variant via `--build-arg` (CUDA base images + `ENABLE_CUDA=ON`);
  use `run_docker_server.sh --gpu` (`--gpus all`). Sample: `samples/gpu_call.yaml`.
- Verified on the CPU-only path (fallback matches `mcl` bit-comparable within MC
  error); the CUDA kernel itself is compiled/run on the RTX host (no GPU in CI).

**Next increments.** Heston-QE on GPU (variance + spot kernels); multi-asset
(Cholesky-correlated factors); a true path-dependent kernel for barriers; QMC on
GPU (Sobol + Brownian bridge) to match the CPU engine's low-discrepancy draws;
and a GPU slave wired into the cluster master.

## Why the current engine does not map to the GPU directly

The MCL engine is a **node dependency-DAG**: `NodeCollector::SortNodes` topologically
orders nodes, `PriceNodes` walks them, and each node's `ComputeValue(dateIndex)`
pulls from its children through virtual calls and pointer chasing
(`_brownian_node->GetValue(...)`, etc.), one scalar path at a time. That design is
flexible (arbitrary payoffs/underlyings) but it is the opposite of what a GPU
wants: SIMT execution needs branch-free, fixed-layout, struct-of-arrays kernels
with no per-element virtual dispatch. Porting the generic graph node-by-node to
CUDA would fight the hardware.

## Recommended approach: a dedicated `mcl_gpu` engine for the hot models

Add a new method (`method: mcl_gpu`) that is a **specialised CUDA path engine**
covering the common, performance-critical models, with the existing CPU node-graph
as the automatic fallback for anything exotic:

- **Covered on GPU:** single- and multi-asset GBM (the `SpotDiffusionNode`
  log-Euler step), Heston via the Andersen QE scheme (`HestonSpotNode`), and Bates
  (QE + compound-Poisson jumps). These are the bulk of real jobs.
- **Fallback to CPU node-graph:** path-dependent/American (LSM), baskets,
  composites, quanto corrections not yet in the kernel — i.e. anything the kernel
  does not implement is routed to today's engine unchanged.

### Kernel design

- **One thread per path** (or per small path-batch), `paths` threads total, grid
  sized to fill the RTX's SMs. Each thread walks the diffusion-date grid, holds its
  own running spot/variance in registers, evaluates the payoff, and accumulates
  into a partial sum/sum-of-squares (block reduction → global) for premium + trust.
- **RNG / QMC:** use **cuRAND**. To preserve the current QMC benefit, keep the
  **Sobol + Brownian-bridge** layout: cuRAND has a scrambled Sobol generator, or
  port the embedded Joe-Kuo (`new-joe-kuo-6.21201`) direction numbers and the
  `PathGenerator` bridge schedule into a device kernel. The bridge's
  lowest-dimension-first ordering maps cleanly to per-thread dimension indexing.
- **Greeks:** the CPU engine already prices base + bump sub-trees in one shared
  path sweep (common random numbers). On GPU the equivalent is to evaluate the
  base and the bumped payoffs from the *same* drawn path inside the kernel — same
  CRN property, near-free extra Greeks.
- **Multi-asset:** correlate per-factor normals with the Cholesky factor (already
  computed on the host) loaded into shared/constant memory.

### Reuse, unchanged

- The YAML config, the node-graph build (used to *derive* model parameters and to
  fall back), and especially the **cluster aggregation**: disjoint Sobol blocks via
  an explicit per-slave skip, path-weighted premium pooling and combined-variance trust all
  carry over verbatim. A GPU slave is just a `-server` whose MCL pricing runs the
  CUDA kernel; the master splits and pools exactly as today.

## Build / tooling implications

- Requires the **CUDA toolkit + an NVIDIA driver**; CMake gains an optional
  `enable_cuda` path (`find_package(CUDAToolkit)`, `.cu` sources), guarded so the
  CPU-only build and the current devcontainer keep working without CUDA.
- **Cannot be developed or verified in this devcontainer** (no GPU, no CUDA
  toolkit). This work must happen on the RTX host.

## Rough effort / payoff

- A first `mcl_gpu` kernel for single-asset GBM (premium + delta/gamma/vega) is a
  focused mini-project; Heston-QE and multi-asset follow incrementally.
- Expected speedup for plain GBM/Heston is large (one RTX vs many CPU cores), and
  it stacks with the cluster: GPU slaves + the existing path-split master.

## Recommendation

Pursue it as a **separate `mcl_gpu` engine** (cuRAND Sobol + Brownian bridge,
one-thread-per-path kernel, block-reduced accumulation), reusing the existing
config and cluster pooling, with the CPU node-graph as the exotic-payoff fallback.
Start with single-asset GBM on the RTX host, validate against the CPU engine
(same price/Greeks to MC error), then extend to Heston/Bates and multi-asset.
