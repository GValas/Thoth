# GPU (CUDA) acceleration

Thoth ships an optional CUDA Monte-Carlo backend, exposed as a capability of the
`mcl` engine: set `allow_gpu: true` in the `mcl_configuration`. It runs single-asset
European vanillas on an NVIDIA GPU and stays on the CPU MCL engine for everything
else — automatically, at runtime. (The legacy `method: mcl_gpu` alias has been
removed: request the GPU with an `!mcl_pricer` + `allow_gpu: true`.)
This guide covers what the engine does, how it is built, how to run it, how the
kernel works, and what is still missing.

> The user + design guide for Thoth's CUDA Monte-Carlo backend.

## What the GPU engine is

The GPU path is a specialised CUDA kernel, *not* a port of the generic CPU
node-graph. The CPU MCL engine walks a dependency DAG one scalar path at a time
(virtual dispatch, pointer chasing) — flexible, but the opposite of what SIMT
hardware wants. So instead of porting that graph, the engine implements a narrow,
branch-free kernel for the hot model and stays on the CPU for anything else.

The wiring lives **inside `PricerMCL`** (`src/tasks/pricer_mcl.{hpp,cpp}`) — GPU is
just a mode of the one MCL engine, not a separate pricer class:

- `PreCheck()` runs the usual MCL checks, then decides once whether to use the GPU:
  `_use_gpu = _configuration->_mcl->_allow_gpu && BookIsGpuSupported()`. With
  `allow_gpu` unset (the default) the GPU is never touched.
- `BookIsGpuSupported()` is **all-or-nothing**: it returns true only if
  `gpu::Available()` *and* every contract in the book fills `GpuGbmParams` via
  `Contract::GPU_GbmParams`. A mixed book never produces a half-GPU/half-CPU
  patchwork — it runs wholesale on the CPU MCL engine.
- In GPU mode, `PriceBook()` prices contract-by-contract (`PriceBookByContract`)
  with per-contract bump-and-revalue Greeks; book MC trust is combined in
  quadrature (FX-scaled to book currency). Otherwise it runs the normal CPU
  diffusion-tree path.

### What is supported on the GPU

Exactly one shape, defined by `Vanilla::GPU_GbmParams` (`src/contracts/vanilla.cpp`):
a **single-asset European vanilla under (deterministic-vol) GBM**. The check
returns `false` (→ CPU fallback) for:

- non-European exercise (American);
- any underlying that is not `KIND_EQUITY` (composite / basket need a multi-asset
  kernel);
- Heston / stochastic-vol underlyings (`HestonOf(...)`), which need the QE scheme,
  not lognormal GBM.

When it returns `true` it fills the forward-measure scalars — `forward` (carrying
carry / dividend / quanto drift), `strike`, `t` (year fraction to maturity),
`vol` (implied vol at strike/maturity), `df` (discount factor), `is_call`. These
are the *same scalars the analytic Black-Scholes pricer uses*, so the GPU MC
agrees with ANA / MCL within Monte-Carlo error.

### Automatic CPU fallback

`gpu::Available()` is the gate. In the CUDA build it returns true iff
`cudaGetDeviceCount` finds a device (`src/tasks/mcl_gpu.cu`); in a CPU-only build
the stub (`src/tasks/mcl_gpu_stub.cpp`) returns `false` unconditionally. Either
way, `allow_gpu: true` is **always safe** — on a CPU-only build, a GPU-less host,
or an unsupported book the engine simply prices on the CPU with identical results,
just no acceleration. `gpu::DeviceInfo()` logs the active device (or why the GPU is
unavailable) at `PreCheck` time, whenever `allow_gpu` is set.

## Build model

CUDA support is a **build-time** switch; the **runtime** fallback is automatic.

`CMakeLists.txt`:

```cmake
option(THOTH_ENABLE_CUDA "Build the CUDA GPU Monte-Carlo engine (needs nvcc + an NVIDIA GPU)" OFF)
```

- **OFF (default)** — the CPU build. No `nvcc`, no CUDA toolkit required. The build
  compiles `mcl_gpu_stub.cpp`, whose `Available()` is `false`. This is what the
  devcontainer and CI use; the CUDA kernel is never seen by the compiler.
- **ON** — requires `nvcc` plus `find_package(CUDAToolkit)`. CMake calls
  `enable_language(CUDA)`, compiles `mcl_gpu.cu` *in place of* the stub, links
  `CUDA::curand` and `CUDA::cudart`, and defines `THOTH_CUDA`. `CMAKE_CUDA_STANDARD`
  is 17 and `CUDA_SEPARABLE_COMPILATION` is on.

The target architecture is `CMAKE_CUDA_ARCHITECTURES`, defaulting to **89**
(`sm_89`, Ada / RTX 40-series). Override it for other GPUs, e.g. `86` (Ampere).

Local CUDA build on a GPU host:

```bash
cmake -B build -DTHOTH_ENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=89
cmake --build build -j
```

> The `.cu` translation unit cannot be compiled in the CPU-only devcontainer (no
> CUDA toolkit / GPU). It is built and exercised on the RTX host. `mcl_gpu.hpp` is
> deliberately self-contained (only `<string>`) so `nvcc` never drags the C++23
> engine headers through the device toolchain.

## Running it

### Docker (recommended)

A single parameterised `Dockerfile` builds both the CPU and the GPU image; the
only differences are the base images (`nvidia/cuda:<tag>-devel/-runtime-ubuntu24.04`)
and the `ENABLE_CUDA=ON` / `CUDA_ARCH=<cc>` build args. The wrapper handles this:

```bash
./run_docker_server.sh --gpu                 # GPU server, sm_89 (default)
./run_docker_server.sh --gpu --arch 86       # GPU server, sm_86 (Ampere)
./run_docker_server.sh --gpu --slaves 4      # GPU cluster: master + 4 GPU slaves
```

`--gpu` makes `run_docker_common.sh`'s `thoth_build_image --gpu <arch>` build the
`thoth-gpu` image (CUDA tag `12.6.3`) and adds `--gpus all` to every `docker run`
so the host GPU(s) are attached. NVIDIA only ships a 24.04 CUDA base from 12.6
onward; 12.6 is binary-compatible (same `libcudart.so.12`). `--arch` sets the
compute capability (`89` = Ada, the default).

Requirements on the host: an **NVIDIA driver** and the **NVIDIA Container Toolkit**
(so `--gpus all` works). Books that are not GPU-supported still price on the CPU
inside the GPU image.

`--gpu` composes with `--slaves` for a **GPU cluster**: each slave is a `-server`
container with `--gpus all`, the master splits paths across them exactly as in the
CPU cluster. Data-parallelism inside one machine (CUDA cores) and across hosts
(the path-split cluster) are orthogonal axes that stack.

Raw `docker` equivalent (the wrapper is preferred):

```bash
docker build -t thoth-gpu \
    --build-arg BUILD_BASE=nvidia/cuda:12.6.3-devel-ubuntu24.04 \
    --build-arg RUNTIME_BASE=nvidia/cuda:12.6.3-runtime-ubuntu24.04 \
    --build-arg ENABLE_CUDA=ON --build-arg CUDA_ARCH=89 .
docker run --rm --gpus all -p 8080:8080 thoth-gpu
```

### Sample

`samples/matrix.yaml` carries the GPU cells `vanilla_mono_eu_call_mcl_gpu` and
`vanilla_mono_eu_put_mcl_gpu` — `!mcl_pricer` with `allow_gpu: true` — alongside
their plain `mcl` / `ana` / `pde` siblings, so the same product is priced on the
device and on the CPU side by side and the prices agree:

```bash
./run_docker_server.sh --gpu
./run_local_client_matrix.sh samples/matrix.yaml --port 8080   # MCL_GPU rows in the table
```

On a CPU-only build (or a host with no GPU) those cells run on the CPU MCL engine
and produce identical results.

## How the kernel works

`src/tasks/mcl_gpu.cu`, entry point `gpu::PriceEuropeanGbm(forward, strike, T,
vol, df, is_call, paths, seed)`:

1. **Forward-measure lognormal terminal.** No time-stepping — the European payoff
   depends only on the terminal, so the host precomputes `a = -0.5 * vol^2 * T`
   (log-drift) and `b = vol * sqrt(T)` (log-vol), and each thread draws one normal:
   `F_T = forward * exp(a + b*z)`, `payoff = max(F_T - strike, 0)` (call) or
   `max(strike - F_T, 0)` (put).
2. **One thread per path (grid-stride).** Threads run a grid-stride loop over
   `paths`, so the grid is sized to the device (`multiProcessorCount * 32` blocks
   of `BLOCK = 256`) and covers any number of paths.
3. **cuRAND Philox normals.** Each thread inits a counter-based
   `curandStatePhilox4_32_10_t` keyed by `(seed, tid)` — cheap, independent per
   thread, and *reproducible*: the same `(seed, grid)` yields identical draws.
   That is the common-random-numbers property the pricer relies on, reusing one
   fixed seed for the base price and every bump (`PricerMCL::PriceContract`) so
   bump-and-revalue Greeks are smooth rather than swamped by MC noise.
4. **Block reduction → atomic accumulate.** Each thread accumulates a local
   payoff sum and sum-of-squares; a shared-memory tree reduction per block
   (`s_sum`, `s_sum2`) is then `atomicAdd`-ed (double atomics, `sm_60+`) into two
   global accumulators.
5. **Premium + trust on the host.** From `mean` and `var` of the payoff,
   `premium = df * mean` and `trust = df * sqrt(var / paths)` (the variance is
   floored at 0 against round-off). `PricerMCL::PriceContract` sets these on the contract.

## Limitations and future work

- **Single-asset European GBM only.** American, barrier / path-dependent,
  stochastic-vol and multi-asset books all fall back to the CPU engine today.
- **No QMC on the GPU.** The kernel uses pseudo-random Philox draws; the CPU
  engine's Sobol + Brownian-bridge low-discrepancy sequence is not yet ported, so
  `use_sobol` in the `mcl_configuration` does not affect the GPU path.
- **Planned increments** (per the source notes): Heston via the Andersen QE scheme
  (variance + spot kernels), Bates (QE + compound-Poisson jumps), multi-asset with
  host-computed Cholesky-correlated factors, a true path-dependent kernel for
  barriers, and GPU Sobol + Brownian bridge to match the CPU draws.

## See also

[running.md](running.md) · [monte_carlo.md](monte_carlo.md) · [volatility.md](volatility.md) · [products.md](products.md) · [pde.md](pde.md) · [agent.md](agent.md)
