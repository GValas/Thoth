# Monte-Carlo engines (MCL / AMC)

This note describes the CPU Monte-Carlo engine `PricerMCL` (`src/tasks/pricer_mcl.{hpp,cpp}`):
how a book is turned into a node graph, how random numbers are drawn, how the
diffusion is discretised, and how American early exercise (the `AMC` label) and
cluster path-splitting work. Everything below is grounded in the source; the
volatility-model detail is cross-linked rather than repeated.

`MCL` and `AMC` are the *same* engine: the log label (and the progress-bar label)
is `AMC` once at least one contract is American and its spot path is being
recorded for Longstaff-Schwartz, otherwise `MCL` (`PricerMCL::LogLabel`).

## 1. The node-graph model

A book is compiled into a directed acyclic graph (DAG) of `MonteCarloNode`s owned
by a `NodeCollector` (`src/core/node_collector.{hpp,cpp}`). Each node knows how to
compute its value at a diffusion-date index (`ComputeValue(DateIndex)`) and which
other (node, date) pairs it depends on (`GetDateDependencies`). Typical nodes:

- `NoiseNode` — one standard-normal draw per date (`src/nodes/noise_node.*`).
- `CorrelatedNoiseNode` — Cholesky combine of the per-underlying white noises
  (`src/nodes/correlated_noise_node.*`); built only for multi-asset books.
- `BrownianNode` — accumulates the Wiener path `W_i = W_{i-1} + sqrt(dt_i)·noise_i`
  (`src/nodes/brownian_node.*`).
- `SpotDiffusionNode` — the diffused spot (`src/nodes/spot_diffusion_node.*`);
  Heston uses `HestonSpotNode` + `HestonVarianceNode` instead.
- `DriftNode`, `LocalVolatilityNode`, `YieldCurveNode`, flow nodes
  (`VanillaFlowNode`, `BarrierFlowNode`, ...), `ContractNode`, `BookNode`.

### Build, sort once, evaluate per path

`Tree_Init` builds the graph in this order: `InitDates` → `SetDiffusionDates` →
`CreateBrownianNodes` → `SetupQuasiRandom` → `CorrelateBrownianNodes` →
`CreateContractualNodes` (which calls `_book->GetNode(_collector)` to materialise
the contract/flow sub-graph), then `SetupAmericanRecording`, then
`SortNodes(roots)`. `NodeCollector::SortNodes` does a DFS from every root plus
every recorded (node, date) pair, then a topological sort into a flat schedule
`(_node_list, _date_index_list)`. Constant nodes (e.g. spot at `t=0`) are dropped
from the schedule (`IsConstant`). Nodes shared by several roots — the base tree
and the Greek-bump sub-trees — appear exactly once, so one path sweep prices the
premium and all single-tree Greeks together.

`Tree_Run` then loops `paths` times; each path calls `NodeCollector::PriceNodes`,
which walks the sorted schedule in order calling `ComputeValue`/`UpdateIndicators`.
Indicators (`GetIndicatorValue`/`GetIndicatorTrust`) accumulate the running mean
and standard error, so the premium and its trust are available at any point.

### The diffusion-date schedule

`PricerMCL::InitDates` builds `_diffusion_dates` as the union of the book's fixing
dates and a regular grid stepped by `max_day_step` calendar days from today to
the last fixing. `NodeCollector::SetDiffusionDates` stores the ascending date list
(`[0]` is today), the index map, and each date's previous-date link. Nodes read
`dt_i` / `sqrt(dt_i)` from this schedule. `vol_year_step` is a finer *variance*
sub-step (year fraction) used inside the vol models, independent of the spot grid.

## 2. Random numbers

**Pseudo-random.** The default source is `Rng` (`src/helpers/rng.hpp`), an
xoshiro256++ generator (Blackman & Vigna) seeded from a single 64-bit value via
splitmix64. It exposes the standard `UniformRandomBitGenerator` interface, so it
also drives `std::poisson_distribution` for the Bates jump node. Gaussian draws go
through the **inverse-CDF** `NormalCdfInv` (Acklam's rational approximation, in
`src/helpers/distributions.hpp`); `Uniform()` returns a value in the *open*
interval `(0,1)` so the inverse-CDF never hits a pole. The single `Rng` instance
is reseeded from `seed` at the start of every `PriceBook`, so the base scenario
and the Greek bumps share common random numbers (stable finite differences).

**Quasi-random (Sobol).** When `use_sobol: true`, `SetupQuasiRandom` builds a
`PathGenerator` and points each underlying's `NoiseNode` at a per-factor buffer of
normalized increments via `SetNoiseBuffer`. The Sobol points come from
`SobolGenerator` (`src/nodes/sobol_generator.*`), a Joe-Kuo direction-number
sequence iterated with the Gray-code recurrence, supporting up to several thousand
dimensions (`MaxDimension()`) — well beyond GSL's old 40-dimension cap. The
all-zero initial point is skipped. Note: a Heston underlying's *variance* noise
(`#vol_white_noise`) and the Bates `#jump_noise` always stay pseudo-random; Sobol
drives only the spot factors.

## 3. Brownian bridge + Sobol dimension allocation

`PathGenerator` (`src/nodes/path_generator.*`) constructs each factor's Brownian
path with a **Brownian bridge** (QuantLib/Glasserman construction in
`BuildBridgeSchedule`): the endpoint `W_T = sqrt(T)·Z_0` is set first, then
successive midpoints `W = left_weight·W_left + right_weight·W_right + std_dev·Z`.
This concentrates the coarse, high-variance structure of the path in the *first*
few standard normals.

The Sobol dimensions are allocated to match: `NextPath` lays out the global
dimension as `dim = step·factors + factor`, so for every factor the endpoint
(bridge step 0) lands in the lowest Sobol dimensions, the first midpoint next, and
so on. Sobol coordinates are most uniform in their leading dimensions, so giving
those to the most important increments is exactly where low discrepancy pays off.
Only the first `_sobol_dim = min(steps·factors, MaxDimension())` dimensions are
quasi-random; the remaining fine increments fall back to pseudo-random
`_rng->Gaussian()`. The buffer exposed to each `NoiseNode` is the *normalized*
increment `n_i = dW_i / sqrt(dt_i)`, so the unchanged `BrownianNode` recurrence
reproduces the bridge path and the downstream Cholesky correlation still applies.

The convergence benefit is large: the README cites a 1y ATM call at 8k paths with
MC error ≈ 0.8 (pseudo-random) versus ≈ 0.01 (Sobol + bridge).

## 4. Diffusion schemes

`SpotDiffusionNode::ComputeValue` takes a **log-Euler** step for `d(lnS)`:

```
expo = (r - v²/2)·dt + v·dW              // exact for a constant v
expo += ½·v·(dv/dlnS)·(dW² - dt)         // log-space Milstein, local vol only
S_i = S_{i-1}·exp(expo)
```

- **Constant volatility** — the log-Euler step is exact; the Milstein term is a
  no-op (it is only enabled via `EnableMilstein` for a local-vol node).
- **Local volatility (SABR → Dupire)** — `LocalVolatilityNode` samples the Dupire
  local-vol surface (built from a SABR smile) on a grid, and the spot node adds the
  log-space Milstein correction using `LogSpotDerivative` (`dv/dlnS`).
- **Stochastic volatility (Heston)** — `HestonSpotNode` + `HestonVarianceNode`
  use Andersen's QE scheme; the spot/variance correlation `rho` is folded into the
  drift coefficients and the residual is an independent Gaussian (`#vol_white_noise`).
- **Bates (Heston + jumps)** — an independent compound-Poisson `JumpNode`
  (`#jump_noise`) is created only when the Heston vol carries jumps (`HasJumps()`).

See [`docs/volatility.md`](volatility.md) for the SABR /
Dupire / Heston-QE / Bates detail.

## 5. American Monte-Carlo (AMC)

American contracts are priced by **Longstaff-Schwartz** least-squares Monte-Carlo
as a post-pass over recorded spot paths, in `PriceAmerican`.

- `SetupAmericanRecording` registers the underlying's exercise-value node (resolved
  via `Contract->GetUnderlying()->GetNode`, so it works for composite/basket as
  well as Mono) for path recording over the exercise grid (`DiffusionIndicesUpTo`
  the maturity). `NodeCollector::RecordPath` snapshots one row per path.
- `FitAmericanPolicy` does **backward induction** from maturity. At each interior
  exercise date the discounted continuation cashflow of the in-the-money paths is
  regressed on the basis `{1, m, m²}`, where `m = S/S0` is moneyness, via
  `LeastSquares` (normal equations). The fitted continuation drives the cashflow
  roll-back: a path exercises when `intrinsic ≥ continuation`, else it holds.
- **Min-ITM guard.** A date is skipped (the policy holds there) unless at least
  `MIN_ITM_FOR_REGRESSION = 50` paths are in the money — otherwise the
  3-parameter fit is exactly determined (interpolation, not regression) and yields
  unreliable continuation values.
- `ApplyAmericanPolicy` walks each path forward and exercises at the first interior
  date whose intrinsic beats the frozen continuation estimate, else takes the
  maturity payoff; the result is `max(MC mean, immediate exercise at S0)`.

**Frozen-policy Greeks.** For single-tree Greeks the exercise boundary is fit
*once* on the base paths and then applied as a **frozen** rule to the base paths
*and* to each bump scenario's recorded (correctly-bumped) spot path. Freezing the
boundary across bumps is cheaper (no re-regression per bump) and lower-variance:
by the envelope theorem a first-order boundary error contributes only at second
order. Each contract's American value replaces its European contribution in the
per-bump premium so `ComputeGreeks` finite-differences the American values.

## 6. Cluster path-splitting

A book whose only root is a single MCL pricer is split across cluster slaves (any
other root — a non-MCL engine, a `!sequence` — is computed by the master itself).
The master splits `paths` as evenly as possible (capping the fan-out at `paths` so
no slave gets zero) and hands each slave:

- a distinct `seed` — so each slave's xoshiro256++ stream is independent, and
- an explicit `sobol_skip` equal to the running path count of the slaves before it
  — so each slave's Sobol block (one point per path) is strictly disjoint, even for
  an uneven split. `SobolGenerator::Skip` fast-forwards the sequence.

The master pools the premium path-weighted and combines the per-slave variances
for the trust. The pooling is **exact**: two 100k-path slaves reproduce a single
200k-path run to machine precision. See [`docs/running.md`](running.md) for the
master/slave commands and the aggregate progress bar.

## 7. Configuration (`mcl_configuration`)

The engine reads an `mcl_configuration` object (`src/tasks/mcl_configuration.hpp`),
referenced from a `pricer_configuration` via its `mcl_configuration` field:

| Field            | Type   | Meaning |
|------------------|--------|---------|
| `max_day_step`  | int    | Coarsest spot diffusion step, in **calendar days**; the grid is the union of this and the book's fixing dates. |
| `min_day_step`  | int    | Lower bound on the diffusion step (calendar days). |
| `paths`          | long   | Number of simulated paths (64-bit, so it can exceed 2³¹). |
| `vol_year_step`  | double | Variance sub-step as a **year fraction** (e.g. `0.01`), used by the vol models. |
| `use_sobol`      | bool   | Use Sobol + Brownian bridge instead of pseudo-random. |
| `seed`           | int    | Seed for the xoshiro256++ stream (default 0; set per slave by the cluster master). |
| `sobol_skip`     | long   | Leading Sobol points to skip (default 0; set per slave so blocks stay disjoint). |
| `node_file`      | string | Optional path for the node dump. |

`PreCheck` requires both an `mcl` object and a correlation matrix (MCL correlates
the underlyings' Brownian motions).

```yaml
# pricer_configuration selects the method and points at an mcl_configuration
my_pricer: !pricer_configuration
  method: mcl                 # pde | mcl | ana  (mcl_gpu = deprecated alias for mcl + allow_gpu)
  mcl_configuration: my_mcl

my_mcl: !mcl_configuration
  max_day_step: 30           # calendar days
  min_day_step: 1
  paths: 100000
  vol_year_step: 0.01         # year fraction
  use_sobol: true
  allow_gpu: false            # true -> CUDA GPU when present + book supported, else CPU
  seed: 0                     # cluster master overrides per slave
  sobol_skip: 0               # cluster master overrides per slave
```

## See also

- [`running.md`](running.md) — running the engine, the cluster master/slave model.
- [`volatility.md`](volatility.md) — SABR → Dupire local vol, Heston QE, Bates jumps.
- [`pde.md`](pde.md) — the finite-difference engine.
- [`products.md`](products.md) — instruments and payoffs.
- [`gpu.md`](gpu.md) — the CUDA `mcl_gpu` backend and its CPU fallback.
- [`agent.md`](agent.md) — working on Thoth as the IT Quant Agent.
