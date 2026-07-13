# Thoth

[![CI](https://github.com/GValas/Thoth/actions/workflows/ci.yml/badge.svg)](https://github.com/GValas/Thoth/actions/workflows/ci.yml)

A C++23 pricing engine for equity derivatives — Monte-Carlo, PDE and analytic
pricers, multi-asset / multi-currency books, driven by a YAML configuration,
usable as a batch tool or as an HTTP service.

---

## Features

**Pricers** (each engine is its own kind/tag: `!mcl_pricer` / `!pde_pricer` / `!ana_pricer`)
- **Monte-Carlo (`!mcl_pricer`)** — correlated geometric Brownian motion via a Cholesky
  factorisation of the correlation matrix (one factor per diffusion step — of the
  step-average matrix — when the correlation is term-structured); exact log-Euler
  step for constant volatility (no discretisation bias). For a **local-vol** surface (`sabr_volatility`;
  mono, basket and composite underlyings) it diffuses the **Dupire** local vol
  sampled onto per-date log-spot grids, with a log-space **Milstein** step that
  cuts the state-dependent
  discretisation bias (automatically a no-op for constant vol). With
  `use_sobol: true` the increments are
  drawn from a **Sobol** low-discrepancy sequence laid out by a **Brownian
  bridge** (the coarsest, most important path structure gets the lowest Sobol
  dimensions, the rest pseudo-random) — far faster convergence on smooth payoffs
  (e.g. a 1y ATM call at 8k paths: pseudo error ~0.8, Sobol ~0.01). The QMC
  treatment covers **every Gaussian factor**: the spot noises (first, so a
  pure-BS book's layout is unchanged), then the Heston/Bates **variance** noises
  and the Hull-White **rate** noises appended after them; only the Bates jump
  source stays pseudo-random (a compound-Poisson draw consumes a variable number
  of uniforms, incompatible with a fixed Sobol dimension). When the
  steps-times-factors budget exceeds the Joe-Kuo table the finest increments
  fall back to pseudo-random, now with a log line saying so. The Sobol
  sequence uses the **Joe-Kuo** direction numbers (`new-joe-kuo-6.21201`,
  embedded), so the low-discrepancy treatment extends to several thousand
  dimensions (well beyond the classic 40-dimension limit) — multi-asset books and finely-stepped
  path-dependent payoffs keep the QMC benefit. Reproducible: the node graph and
  factors are name-ordered, so results are independent of heap layout / build /
  platform.
- **PDE (`!pde_pricer`)** — Crank-Nicolson finite-difference scheme; supports **American**
  exercise (backward induction `max(intrinsic, continuation)`), a 2-D `(S,v)`
  Douglas-ADI grid for **Heston** (and **Bates** as a PIDE — the ADI plus an
  explicit jump integral, IMEX) whose resolution follows `vanilla_precision`, and
  the **variance swap** fair strike as a backward expected-accumulated-variance
  solve driven by the **Dupire local variance** per node (so it integrates the
  smile, matching the analytic static replication).
- **Analytic (`!ana_pricer`)** — closed-form (Black-Scholes) for instruments that admit
  it.
- **GPU acceleration (`!mcl_pricer` + `allow_gpu`)** — the `mcl` engine offloads to a CUDA
  backend when the `mcl_configuration` sets `allow_gpu: true`, a usable NVIDIA GPU
  is present, and the whole book is GPU-supported (single-asset European-vanilla
  GBM: one thread per path, cuRAND, block-reduced payoff, per-contract bump Greeks
  under a fixed kernel seed for common random numbers; NB the block-reduction
  uses atomic float adds, so GPU premia are reproducible only up to FP summation
  order — run-to-run wiggle at the last ulp — unlike the bit-reproducible CPU
  engine). Built only with
  `-DTHOTH_ENABLE_CUDA=ON`; on a CPU-only build, a host with no GPU, or any book it
  does not yet support (American / barrier / stochastic vol / multi-asset) it
  **runs on the CPU `mcl` engine** instead — so `allow_gpu` is always safe to set.
  See [`docs/gpu.md`](docs/gpu.md).

Long runs show a `│███░░░│` progress bar with the running price/trust. On a
terminal it redraws in place as a single updating line; when stdout is not a TTY
(output redirected, or captured via `docker logs`) it falls back to one bar line
every 10% so logs stay readable instead of piling up the redraw frames. A pricer
can attach a `debug_configuration` with `generate_nodes_graph: true`: each MCL tree
then returns its Monte-Carlo node graph as Graphviz `.dot` text in the result block
— `<tree>_mcl_graph`, the base `premium_mcl_graph` plus one graph for every Greek
tree built (`delta_mcl_graph` / `gamma_mcl_graph` / `vega_mcl_graph` /
`rho_mcl_graph` / `theta_mcl_graph` on a bump-and-revalue book; the single-tree
Greek scenarios on a Mono book key theirs `delta_<udl>` / `gamma_up_<udl>` / … ).
The `.dot` is emitted as a YAML literal block (readable, not a `\n`-escaped line)
and travels back in the result (HTTP response / batch output / through the cluster),
so there is no `.dot` or `.png` file written to disk.

**Greeks** — list any of `delta`, `gamma`, `vega`, `rho`, `theta` in a pricer's
`indicators` (alongside `premium`). All three engines use bump-and-revalue. The
first-order Greeks (delta, vega, rho, theta) use a **one-sided** difference that
reuses the base price (`(P(x+ε) − P₀)/ε`), so each costs one extra valuation;
gamma uses a **central** second difference (a wider spot bump, since the second
difference is otherwise swamped by the PDE grid's per-spot re-centering). Each
engine then computes them the cheapest way for its structure:
- **PDE / ANA** bump and reprice **per contract**, inside the single contract
  loop, so one progress bar covers price + Greeks and the per-contract Greeks are
  also reported (not just the book total).
- **MCL** builds the base tree and every spot/vol/rate bump sub-tree into **one**
  node graph that shares the (expensive) Brownian/noise nodes, and prices them in
  a **single path sweep** — so delta/gamma/vega/rho cost one simulation, not one
  per bump. theta is a separate one-day reprice (a second, base-only sweep, shown
  under its own `MCL theta` / `AMC theta` progress bar). Sharing the path nodes is exact
  common-random-numbers, so the Greeks match the old reset-per-scenario values
  bit-for-bit. Each bump scenario also carries a node **per contract**, so MCL now
  reports **per-contract Greeks** too (attributed by differencing each contract's
  scenario node — the book Greek is exactly their fx-weighted sum); they carry the
  usual Monte-Carlo noise the book total averages out. Books with American contracts
  or non-trivial (basket/composite) underlyings fall back to book-level bump-and-revalue.

Units: delta `dP/dS`, gamma `d²P/dS²`, vega per vol point, rho per 1% parallel
rate move, theta per calendar day. On a 1y ATM call the three engines agree and
sit close to Black-Scholes (delta ≈0.67, gamma ≈0.012, vega ≈0.37, rho ≈0.50,
theta ≈−0.026 — delta carries the small one-sided-bump bias ½·gamma·S·ε).

For **basket / rainbow** underlyings (each component rebased to 100 at inception),
the spot bump scales the component against its *fixed* inception level `S_i0`, so
delta/gamma respond instead of cancelling — ANA and MCL agree (e.g. a 1y ATM basket
call: delta ≈0.673 on both). The PDE prices a basket on a 1-D grid in basket-spot
space, so it reports the grid's own `dV/dS` delta (matches ANA/MCL); its grid
*gamma* there is second-difference-noisy (use ANA/MCL for an accurate basket gamma).
The moment-matched basket vol is **strike-dependent** (the shifted-lognormal fit
has skew), so the PDE grid diffuses the vol at the **contract's strike** — the
same vol the ANA closed form prices at — keeping the two engines in agreement
away from the money even when the component vols are widely dispersed (the ATM
"representative" vol still sizes the grid and the quanto correction).

**Model-parameter Greeks** — for the stochastic / local-vol surfaces you can also
request `vega_<param>` indicators that bump one model parameter and revalue (per
unit parameter): SABR `vega_alpha` / `vega_beta` / `vega_rho` / `vega_nu`, Heston
`vega_v0` / `vega_kappa` / `vega_theta` / `vega_xi` / `vega_rho`, and Bates' jumps
`vega_jump_intensity` / `vega_jump_mean` / `vega_jump_vol`. They work on any engine
that prices the model, but only the **ANA** engine (Hagan / characteristic-function
reprice) gives them reliably — the PDE grid and the SABR Dupire-local-vol diffusion
don't yield clean parameter sensitivities, so `samples/matrix.yaml` requests them on
the Heston ANA cell (its SABR and Bates cells price premium only). A parameter no
underlying's surface exposes is silently skipped.

**Instruments**
- `vanilla` — call / put, **european** or **american**, absolute or
  relative strike (`is_absolute_strike: false` books the strike as a **percent of
  the underlying's spot** at the valuation date — the rebased 100 for a basket /
  rainbow — resolved once against the unbumped spot, so the Greeks bump the spot
  and never the strike; the same convention covers a `barrier`'s vanilla strike,
  while barrier *levels* stay absolute). **Quanto** (a foreign-currency asset paid in the book
  currency) is supported by all three engines — the drift correction
  `F *= exp(-ρ·σ_S·σ_X·t)` lives in the MCL node graph, ANA's quanto forward and
  the PDE carry, and the three agree (American quanto via PDE / MCL). The settlement
  FX (`σ_X`, spot) is **triangulated through the pivot** when the asset and payoff
  currencies are both non-pivot (only the pivot basis, e.g. `usd/eur`, `usd/jpy`, is
  ever stored): `var(eur/jpy) = var(usd/eur) + var(usd/jpy) − 2ρ·σ·σ`, so an
  EUR-asset / JPY-payoff cross quanto prices (the `eur/jpy` cross is never a book object).
  A **`composite`** underlying (S·FX *diffused*, not the fixed-FX quanto drift) between two
  non-pivot currencies is supported too: MCL builds the cross FX as the ratio of the two pivot
  legs and removes the ratio-of-lognormals convexity with a deterministic
  `exp(−(σ_PA² − ρ·σ_PA·σ_PB)·t)` factor, so the composite forward matches the single-lognormal
  one ANA/PDE use — the three engines agree (a naive leg-ratio over-forwards MCL ~0.4%). The
  correction assumes a **constant** cross-FX correlation; a cross composite under a
  term-structured correlation is refused rather than mis-corrected.
- `barrier` — knock-out / digital payoffs, **continuous or
  discrete monitoring** (`monitoring_period_days`); PDE, MCL and (continuous-only)
  closed-form.
- `variance` — pays `notional * (realized_variance - strike_variance)`;
  priced by **Monte-Carlo** (realized variance of the simulated path),
  **analytically** by static replication (the 1/K²-weighted option strip, so a
  smile feeds in), or by **PDE** (the fair variance as a backward
  expected-accumulated-variance grid solve). The three agree (1y, 30% flat,
  ~461). **Discrete observation** via `observation_period_days` (fixings at
  `today + k*period` up to maturity): the MCL samples the realized variance on
  that schedule (the fixing dates are forced into the diffusion grid), while ANA
  and PDE add the deterministic per-interval drift² term
  `Σ (log(F(t₂)/F(t₁)) − v_fwd/2)² / T` on top of their continuous fair variance,
  with `v_fwd = σ²(t₂)t₂ − σ²(t₁)t₁` each interval's **forward ATM implied
  variance** (exact under flat BS — where it reduces to `σ²Δt` — and a
  per-interval ATM approximation under a smile, so a sloped term structure
  prices each interval's convexity at its own vol; pinned against the engine's
  own surface on a steep 15%→35% term structure in `tests/test_variance.cpp`)
  — the three engines agree on a monthly-fixing swap where the add-on is ~7% of
  the fair variance.
  `0`/absent keeps the continuous convention (every diffusion step).
  **Seasoned (in-life) swaps**: an optional `start` date in the past plus a
  `fixings` reference (a `!simple_fixing_data` holding the realised
  observations) price a swap already running: the realised leg is the sum of
  squared log-returns over the past fixings — validated against the observation
  schedule, closed by the **last-fixing → spot bridge** — and every engine
  time-weights it with its own future fair leg,
  `fair_total = (past_sum2 + fair_future·T_future)/T_total`. The discrete
  schedule stays anchored on `start` so past and future fixings align; the
  bridge makes the position spot-sensitive, and ANA/PDE report its analytic
  delta/gamma (a fresh swap stays first-order neutral). Splitting the interval
  running through today drops the drift-level cross term — the standard
  mid-life convention, and exactly what the MC path (restarting at the live
  spot) realises, so the three engines agree by construction
  (`tests/test_seasoned_varswap.cpp`, demo `samples/seasoned_varswap.yaml`).
- `autocallable` — an autocallable note, **Athena or Phoenix** flavour:
  explicit `autocall_dates` (strictly between today and maturity); at the first
  observation with the spot at or above `autocall_barrier` the note redeems
  early. **Athena** (no `coupon_barrier`) pays `nominal * (1 + k*coupon)` on
  redemption (the accrued "snowball" coupon, k the 1-based observation count)
  and, at maturity, the full accrued coupon above the autocall level, the bare
  nominal above `protection_barrier`, and `nominal * S_T/S_ref` below (linear
  capital at risk). **Phoenix** (`coupon_barrier` set, ≤ the autocall level)
  detaches the coupon **per period**: every alive observation (and maturity)
  with the spot at or above the coupon barrier pays `coupon*nominal` **at its
  own date** (own discounting — pathwise under Hull-White); the early
  redemption pays the bare nominal (that date's coupon rides along), and with
  `coupon_memory: true` a paying date also recovers the consecutively missed
  coupons. Levels are **percent of the valuation-date spot**, resolved once
  against the unbumped spot (the relative-strike sticky-cash convention, so all
  Greeks bump the spot and never the levels). **MCL** prices every flavour
  pathwise — one flow node per schedule date, only the first trigger redeems;
  the autocall is an automatic trigger (not an optimal exercise), so no LSM is
  involved and the per-date pathwise discounting composes with **multi-curve
  and Hull-White** books out of the box. **PDE** runs a backward induction
  overwriting the layer at each observation step (the rebate above the autocall
  level; plus, for a Phoenix, the period coupon added in the [coupon, autocall)
  zone) — with a **Rannacher restart** (two fully implicit steps after each
  overwrite) damping the Crank-Nicolson oscillations the discontinuity would
  otherwise inject, and the top Dirichlet boundary following the **discounted
  next rebate**; the **memory flavour is MCL-only** (the missed-coupon count is
  not a function of the spot alone, so the 1-D grid rejects it). PDE and MCL
  agree (pinned against an independent transition-density convolution oracle
  and closed-form coupon strips in `tests/test_autocallable.cpp` /
  `tests/test_phoenix.cpp`); **ANA rejects** the product (no closed form).
  Seasoned (already-running) autocallables are not supported yet.
- `asian` — an **arithmetic average-price** option: at maturity pays
  `nominal * max(ω*(A − K), 0)` with `A` the arithmetic mean of the spot over
  the averaging schedule (`observation_period_days`, monthly by default, up to
  and including maturity), `ω = ±1` for call/put and `K` absolute or relative
  (percent of spot). Averaging damps the terminal variance, so it prices below
  the equivalent vanilla. Path-dependent → **Monte-Carlo only** (a single
  observation reduces exactly to the vanilla; ANA/PDE reject —
  `tests/test_asian_ratchet.cpp`).
- `ratchet` — a **cliquet** note: pays
  `nominal * clip(Σ clip(Rᵢ, local_floor, local_cap), global_floor, global_cap)`
  over the period returns `Rᵢ = S(tᵢ)/S(tᵢ₋₁) − 1` on the boundary schedule
  (today, +k·period, …, maturity). A capped period gain is **locked in** (it
  cannot be given back by a later fall), and the `global_floor` (default 0) is
  the note's capital protection; `global_cap` is optional. Floors/caps are in
  percent. Path-dependent → **Monte-Carlo only** (a locally-flat note pays the
  deterministic global floor; ANA/PDE reject — `tests/test_asian_ratchet.cpp`).
- `digital` — a European **binary / digital** option: at maturity pays a fixed
  cash amount (`payout: cash_or_nothing`, `cash_amount` = Q) or the spot
  (`payout: asset_or_nothing`) **iff in the money** (S > K call / S < K put).
  Path-independent → priced by **ANA** (closed form `Q·df·N(±d₂)` /
  `df·F·N(±d₁)`), **PDE** and **MCL**, and the three agree. Pinned by exact
  identities (cash call + put = df; vanilla = asset-or-nothing − K·cash-or-nothing;
  `tests/test_digital.cpp`). *Note:* a digital struck **at the money** is the
  classic discontinuity worst case for the PDE grid (~few % off at S = K); ANA is
  exact and MCL validates it, and the grid is accurate away from the strike.

**Underlyings**
- `equity`, `composite` (compo / quanto), `basket`, plus `currency` /
  `forex` for FX.

**Market data**
- `yield_curve`, `repo_curve`, `continuous_dividends_curve`,
  `discrete_dividends`, `correlation_matrix`. Curves carry a `dates`/`values` term structure and are
  read by **linear interpolation on the (continuously-compounded) rate** between
  pillars (ACT/365 weight), held flat beyond the first/last pillar.
  A `currency` names its **projection / funding** curve (`rate` — every forward
  and drift grows on it) and, optionally, a distinct **OIS / collateral** curve
  (`discount_rate`) that all cash flows are **discounted** on (multi-curve): the
  ANA discount factors, the PDE per-step discount rates, the MCL `ContractNode`
  and the American LSM discounting all read the OIS curve, while forwards, FX
  covered-parity drifts and quanto carries stay on the projection curve. The rho
  Greek bumps both curves in parallel. Omitting `discount_rate` reduces exactly
  to the historic single-curve behaviour. See `samples/multicurve.yaml`.
  A `currency` may further carry a **stochastic short rate** (`rate_model`, a
  `hull_white` object: `mean_reversion` a > 0, `volatility` σ_r in percent) —
  the one-factor **Hull-White equity-rate hybrid**. The initial discount curve
  is fitted **by construction** (the engine works on the OU factor
  x, r = x + α(t), with the ∫α convexity in closed form — θ(t) never appears):
  the **MCL** diffuses x with the exact OU transition, discounts **pathwise**
  with exp(−∫r) and drifts the equity at the stochastic rate (the only
  discretisation bias is the trapezoid ∫x, second order in the step); the
  **ANA** prices the BS+HW European vanilla in closed form — unchanged forward
  and OIS df, effective T-forward variance
  σ_eff²T = σ_S²T + 2ρσ_Sσ_r∫B + σ_r²∫B², B(t) = (1−e^{−a(T−t)})/a. The
  equity/rate correlation ρ is the matrix entry against the pseudo-single
  **`<currency>_ir`** (the `<name>_var` convention; pillar-constant under a
  term-structured matrix, validated). Calibration of (a, σ_r) to swaptions is
  out of scope: both are direct inputs. See `samples/hull_white.yaml`.
  A `correlation_matrix` takes either a full row-major `matrix` or a lower-triangular
  `symmetric_matrix` (diagonal included, length `n(n+1)/2`); either way it must be
  symmetric positive-definite (validated at load). With an optional `maturities`
  pillar list (year fractions, strictly increasing) the same flat field carries one
  matrix per pillar **concatenated**, and the correlation becomes **term-structured**:
  entries are interpolated linearly in time between pillars and held flat beyond
  (a convex combination of correlation matrices stays one, so every interpolated
  matrix is valid; each pillar is validated positive-definite at load). The engines
  consume exact integrated views of the piecewise-linear structure: the analytic
  quanto / composite / basket-moment formulas read the running average
  `rho_bar(T) = (1/T)*int_0^T rho`, the PDE telescopes the per-step quanto carry,
  and the Monte-Carlo correlates each step's increments with the Cholesky factor
  of the **step-average** matrix (so the integrated covariance is reproduced
  exactly wherever the diffusion dates fall). The spot/variance `<name>_var`
  entries of a stochastic-vol underlying must be identical across pillars (the
  engines consume a single scalar ρ; validated at load). See
  `samples/term_correlation.yaml`.
  `discrete_dividends` is an (ex-`dates`, cash `amounts`) schedule on an equity,
  priced by the **escrowed-dividend model**: the forward (and the MCL diffusion
  spot) net the present value of the dividends due before maturity off the spot,
  so the ANA, PDE and MCL engines all price the same escrowed forward. American
  early exercise tests the payoff against the **observed spot** (escrowed value +
  the PV of the still-future dividends), not the dividend-stripped escrowed value,
  so the PDE and MCL American prices agree on dividend-paying equities.
- Volatilities: `bs_volatility` (flat Black-Scholes vol), `sabr_volatility`
  (Hagan 2002 lognormal SABR implied surface, per-maturity `alpha`/`beta`/`rho`/`nu`,
  with **arbitrage-free wings**: beyond ±2.5 ATM-sigma of log-moneyness the surface
  switches to Benaim-Dodgson-Kainth power-law price tails matched in value and
  slope to the Hagan prices at the cutoff — the tails have a strictly positive
  implied density by construction, so the far wings no longer poison the Dupire
  local-vol surface the MCL/PDE engines diffuse), `heston_volatility` (genuine
  stochastic vol — see below) and `lsv_volatility` (local-stochastic vol — see
  below).

**Stochastic volatility (Heston / Bates)** — `heston_volatility` (`init_vol`/`long_vol`/
`kappa`/`vol_of_vol`, vols in percent; the spot/variance correlation ρ lives in
the global `correlation_matrix` as the underlying against its variance
pseudo-underlying `<name>_var`) is priced consistently by all three engines: MCL via
the Andersen QE variance scheme, ANA via the characteristic function (Carr–Madan /
Little-Heston-Trap), and PDE via a 2-D `(S,v)` Douglas-ADI grid (European and
American). The three agree to ~0.3% across moneyness, and the degenerate
(vol-of-vol → 0) limit reproduces Black-Scholes. Adding lognormal
(compound-Poisson) jumps — `jump_intensity` (λ per year), `jump_mean`, `jump_vol`
— turns it into the **Bates** model: priced by **MCL** (an independent jump node
on the QE spot), **ANA** (the closed-form jump characteristic function multiplies
the Heston CF) and **PDE** (the Bates PIDE — the Heston ADI plus an explicit
log-spot jump-integral term, IMEX, with the `−λk̄` compensator in the drift). The
Heston, Bates and SABR cases are exercised by the cells in `samples/matrix.yaml`.

**Local-stochastic volatility (LSV)** — `lsv_volatility` (the Heston fields plus
`surface:`, a reference to a deterministic target surface — `sabr_volatility` or
`bs_volatility`) diffuses a Heston variance factor whose spot coefficient is
multiplied by a leverage `L(S,t)` calibrated so the model **reprices the target
implied surface** (Dupire matching `L²(s,t)·E[v_t|S_t=s] = σ²_dupire(s,t)`,
estimated by a binned particle method — a fixed-seed 16k-path pre-pass in
`Single::CalibrateLeverage`). Vanillas then match the target surface while
exotics keep Heston's forward-smile dynamics — the standard exotic-desk setup a
pure local-vol or pure Heston model can't deliver. Priced by **MCL** (the QE
variance plus a leveraged Andersen spot step; the leverage grid is read along the
path like the Dupire local-vol node) and **PDE** (the 2-D `(S,v)` ADI with
`L²v`/`Lv` in the S-direction and cross coefficients); **ANA rejects it** (no
closed form — silently pricing the bare Heston CF would ignore the leverage).
Bates jumps are not supported under LSV. See `samples/lsv.yaml` for a
calibration-quality demo (SABR reference vs LSV MCL/PDE on the same call); the
`samples/matrix.yaml` sequence also carries an LSV MCL/PDE cell alongside the
Heston / Bates / SABR ones.

**Analytics objects**
- `pricer`.
- `sequence` — a task that runs a list of other tasks in order, each writing its
  own result block (e.g. price a whole book of cases in one process).

---

## Documentation

In-depth guides live in [`docs/`](docs/) (see the [index](docs/README.md)):
[architecture](docs/architecture.md) · [running](docs/running.md) ·
[products](docs/products.md) · [volatility](docs/volatility.md) ·
[Monte-Carlo](docs/monte_carlo.md) · [PDE](docs/pde.md) · [GPU](docs/gpu.md) ·
[agent workflow](docs/agent.md).

---

## Build

Dependencies (Debian / Ubuntu):

```bash
sudo apt-get install -y build-essential cmake pkg-config \
    libboost-all-dev libyaml-cpp-dev libcpp-httplib-dev
```

| Library      | Use                              |
|--------------|----------------------------------|
| Boost        | date handling + Boost.Math (gamma CDF, Gauss-Kronrod quadrature) |
| yaml-cpp     | configuration format             |
| cpp-httplib  | HTTP server / client             |

```bash
cmake -B build
cmake --build build -j        # -> ./build/thoth
```

A devcontainer (`.devcontainer/`) and a production `Dockerfile` (multi-stage,
lean runtime image) are provided. The devcontainer preinstalls the C++ toolchain
extensions plus **Graphviz Interactive Preview** (`tintinweb.graphviz-interactive-preview`),
so a `generate_nodes_graph` `.dot` (extracted from the result's `*_mcl_graph`
field) can be previewed in-editor without an external `dot` render.

#### GPU (CUDA) build

The CUDA backend is off at build time by default. On a machine with the CUDA
toolkit (`nvcc`) and an NVIDIA GPU, enable it with:

```bash
cmake -B build -DTHOTH_ENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=89   # 89 = Ada / RTX 40-series
cmake --build build -j
```

Or build/run the GPU Docker image via `./scripts/run_docker_server.sh --gpu` (needs the
NVIDIA Container Toolkit). The CPU and GPU images come from the **same**
parameterised `Dockerfile`, and the same `scripts/run_docker_server.sh` wrapper runs
both: `--gpu` swaps in the `nvidia/cuda` base images and `ENABLE_CUDA=ON` through
`--build-arg` and attaches the host GPU with `--gpus all` (`--arch` sets the
compute capability, default 89). Without CUDA the `mcl` engine still builds and an
`allow_gpu` book simply runs on the CPU at run time.

The **devcontainer** ships the CUDA toolkit (`nvcc`, installed in its Dockerfile) and
requests the host GPU (`hostRequirements.gpu: optional`, needs the NVIDIA
Container Toolkit on the host), so on a GPU host you can build and run the engine
in-container directly:

```bash
cmake -B build-gpu -DTHOTH_ENABLE_CUDA=ON && cmake --build build-gpu -j
./build-gpu/thoth -batch samples/matrix.yaml /tmp/out.yaml   # the allow_gpu cells run on the GPU
```

On a host with no GPU the devcontainer still opens (CPU-only, an `allow_gpu` book
runs on the CPU).

### Tests

A doctest suite (`tests/`) covers European/American vanillas, barriers,
dividends, relative strikes (the relative booking pricing exactly as its
absolute-cash twin — premium and Greeks — plus the basket PDE agreeing with ANA
away from the money), multi-asset consistency, engine-vs-engine agreement (incl. quanto),
variance swaps (analytic vs the accumulated-variance PDE), baskets, composites,
the SABR surface, Sobol QMC, the `!sequence` task, the bump-and-revalue Greeks
(delta/gamma/vega/rho/theta vs Black-Scholes) and the model-parameter
`vega_<param>` Greeks (Heston / SABR), book aggregation, determinism, config
parsing, and term-structured curves (steep-curve European agreement across the
three engines, plus the American LSM **and** the American PDE against an
independent term-structure binomial oracle, the local-vol PDE repricing the
SABR smile across strikes, and a three-engine quanto-on-SABR agreement pin —
all engines apply the quanto drift correction at the ATM implied vol). It is built alongside the binary:

```bash
cmake --build build -j           # builds thoth + thoth_tests
ctest --test-dir build --output-on-failure
```

### Formatting

`./scripts/format.sh` runs clang-format (style in `.clang-format`) over `src/` and
`tests/`; `./scripts/format.sh --check` fails on unformatted files (CI gate).
`clang-format` is preinstalled in the devcontainer (and the production build
image); for a manual setup, `sudo apt-get install -y clang-format`.

### Continuous integration

`.github/workflows/ci.yml` (GitHub Actions) runs on every push and pull request
to `main` and mirrors the local checks above on `ubuntu-24.04`:

- **build-test** — `cmake -B build` → `cmake --build build -j` → `ctest` (g++,
  Release, the same Debian/Ubuntu deps; doctest's header is fetched by the test
  CMake when absent). Built with `-DTHOTH_WERROR=ON`, so the `-Wall -Wextra`
  warnings are fatal in CI (they stay non-fatal locally);
- **sanitizers** — a Debug build with `-DTHOTH_SANITIZE=ON`
  (AddressSanitizer + UndefinedBehaviorSanitizer) running the full `ctest`, to
  catch memory / UB bugs the Release build hides;
- **format** — `./scripts/format.sh --check` with `clang-format-18` (the version that
  produced `.clang-format`).

Both hardening switches are off by default in `CMakeLists.txt`, so a plain local
build stays fast and tolerant. The status badge at the top of this README
reflects the latest `main` run.

### Debugging (VS Code)

`.vscode/launch.json` ships two gdb configurations — **Debug thoth -batch**
(prompts for the input YAML) and **Debug thoth -server 8080**. Press **F5**:
the *cmake: build debug* task first builds an unoptimized `-g` binary into
`build-debug/` (the Release `build/` is left untouched), then gdb launches it so
breakpoints in the engine hit on the right lines. Debug runs are slower (`-O0`),
so debug with a small book or a reduced `paths`.

---

## Usage

### Batch (file in / file out)

```bash
./build/thoth -batch <input.yaml> <output.yaml> [task_name]
# or build the production image and price in a container — the result is written
# next to the input as <input>.out.yaml, owned by the invoking user:
./scripts/run_docker_batch.sh samples/simple_call.yaml          # -> samples/simple_call.out.yaml
```

The output YAML is the input config with the computed `*_result` blocks added.

To exercise every engine at once, `samples/matrix.yaml` is a `!sequence`
task that runs the full pricer/product matrix (vanilla european/american — call &
put, quanto, composite, basket / best-of / worst-of, up/down & in/out, continuous &
discrete barriers, American Heston, variance swap, Heston, Bates, and **SABR
local-vol** — across PDE / MCL / ANA, with Sobol and pseudo-random MCL; plus the
2026 features — **term correlation**, **multi-curve/OIS**, the **Hull-White
hybrid**, **seasoned variance swaps**, **autocallables** (Athena / Phoenix /
memory), the path-dependent **Asian** and **ratchet** notes, and the
**cross-currency** (both-non-pivot, triangulated-FX) **quanto** and
**composite**) in one
process — price it like any other book (`-batch`, or post it to a server with the
built-in `-client`). `scripts/run_local_client_matrix.sh` posts it and prints a
per-product table (method, time, premium and every Greek) — see below.

`samples/big_option.yaml` is a single feature-dense option (an American, USD-quanto
put on a EUR equity with a SABR local-vol surface, continuous dividends, a repo
spread and discrete cash dividends) priced by MCL with the node-graph dump on
(`debug_configuration.generate_nodes_graph`) — `-batch` it and read the
`premium_mcl_graph` field in the output YAML (the Graphviz `.dot` of the node
graph); pipe it to `dot -Tpng` to view it.

`samples/random_vanillas.yaml` is a stress book: 1000 random European vanillas
(random strike / type / maturity, seed 42) on one equity, priced in a single MCL
sweep with all Greeks. Its `mcl_configuration` sets `allow_gpu`, so the whole book
runs on the GPU GBM kernel on a CUDA build (CPU fallback otherwise). The helper
`scripts/build_run.sh [--gpu] [input.yaml]` builds (with `-DTHOTH_ENABLE_CUDA=ON` when
`--gpu` is passed) and prices it.

`samples/lsv.yaml` is the LSV calibration-quality demo: one 1y ATM call priced
three ways in a `!sequence` — ANA on the raw target SABR surface (the reference
the calibrated model must reproduce), then the LSV model (a deliberately
off-level Heston base + calibrated leverage) through MCL and PDE. The three
premiums agree to MC / 2-D-grid error, demonstrating the Dupire matching.

`samples/autocallable.yaml` is the structured-note demo: a 2y Athena
autocallable (three semiannual 100% observations, 8% snowball coupon, 60%
protection) priced by the PDE backward induction and the pathwise MCL — they
agree (~104.4 on a 100 nominal, matching the transition-density oracle) — plus
the same note under a Hull-White short rate (the book's first genuinely
rate-sensitive product), and the Phoenix flavour (2% per-period coupon above a
70% coupon barrier: PDE ≈ MCL ≈ 99.54, and the memory variant worth ~0.06 more
through the coupon catch-up, MCL).

`samples/seasoned_varswap.yaml` is the in-life variance swap demo: a swap
started 6 months ago (30-day observations, realised leg from a
`!simple_fixing_data` at ~40% realised vol vs a 20% future implied), priced by
the three engines — they agree (~−44.9 on 10000 notional, strike 25), and all
three report the same bridge delta (ANA/PDE analytic, MCL by bump — identical
to 10 decimals, the future strip being first-order neutral).

`samples/hull_white.yaml` is the equity-rate hybrid demo: a 1y ATM call on a
EUR equity whose currency carries a Hull-White short rate (a = 10%, σ_r = 2%,
ρ(eq, eur_ir) = +0.5) over the multi-curve setup — ANA (effective-vol closed
form, ≈ 16.38) and MCL (pathwise stochastic discounting) agree within the MC
error; the positive equity/rate correlation raises the price vs the
deterministic-rate 16.19.

`samples/multicurve.yaml` is the multi-curve / OIS demo: one 1y ATM call priced
by the three engines with an 8% projection curve and a 5% OIS `discount_rate`
on the currency — all three agree on Black(F at 8%, df at 5%) ≈ 16.19, and the
ANA/MCL rho Greeks agree under the joint two-curve bump.

`samples/term_correlation.yaml` is the term-structured correlation demo: a 3m/9m
two-pillar matrix over two equities and one FX pair (asset/FX correlation decaying
0.8 → −0.4, eq/eq 0.9 → −0.3), pricing a 1y USD-quanto call by ANA (exact
integrated ρ) and MCL (per-step Cholesky — agrees within MC error) plus a 50/50
basket call under the decaying eq/eq correlation.

`samples/termsheet.yaml` is the documentation-task demo: a `!termsheet` task
renders the Phoenix autocallable's booked description as a Markdown termsheet
(header with the levels resolved against the as-of spot, the flavour-specific
payoff clause, the observation schedule, a disclaimer) into its result block as
a `termsheet` literal field — supported for vanillas, barriers, variance swaps
(incl. seasoned) and autocallables (Athena / Phoenix); pure documentation, no
pricing.

`samples/test.yaml` is a small cross-engine sanity book: three European vanillas
(ATM call, OTM put, ITM call) on one equity, each priced by PDE, MCL and ANA — plus
the ATM call also on the GPU MCL engine (`allow_gpu`, CPU fallback off-CUDA) — as a
`!sequence`, so the three engines agree per option to MC error. Tabulate it with
`scripts/run_local_client_matrix.sh samples/test.yaml`.

### HTTP pricing service

```bash
./build/thoth -server 8080                       # POST /price, GET /health
# or build the image and serve from a container:
./scripts/run_docker_server.sh --port 8080

curl --data-binary @samples/simple_call.yaml localhost:8080/price
# the built-in client:
./build/thoth -client http://localhost:8080 samples/simple_call.yaml
# post a !sequence book (e.g. the matrix) and print a per-product table — method,
# time spent, premium and every Greek (--raw keeps the full YAML response):
./scripts/run_local_client_matrix.sh samples/matrix.yaml --port 8080
```

`POST /price` takes a YAML body and returns the YAML result; an optional
`X-Task-Name` header selects which object to run (default: the `root` object).
Send `Content-Type: application/x-yaml` for bodies over ~8 KB (the built-in client
and `scripts/run_local_client_matrix.sh` already do); the default form content type is
capped by the HTTP library.

Requests are serialised (the engine shares global progress/cancellation state): the server logs
the client IP on arrival and again if a request has to wait. If a client
disconnects mid-pricing the run is cancelled, freeing the server instead of
finishing a result nobody will read. `scripts/run_docker_server.sh` passes `docker run -t`, so
the server console shows the live single-line progress bar; captured later via
`docker logs` (no live TTY) it reads as one bar line every 10%.

### Cluster (distributed Monte-Carlo)

A **master** can spread a Monte-Carlo book's paths across several **slave**
servers (ordinary `-server` instances), dispatch the sub-requests over HTTP and
aggregate the results:

```bash
./build/thoth -server 8091 &                     # slaves
./build/thoth -server 8092 &
./build/thoth -cluster 8090 http://localhost:8091 http://localhost:8092
# or the Docker wrapper with --slaves: one container per slave + a master, all on a
# private network (Ctrl-C, or the master exiting, tears the whole cluster down):
./scripts/run_docker_server.sh --port 8090 --slaves 2
# then POST a book to the master (single-MCL-pricer books get path-split):
./build/thoth -client http://localhost:8090 samples/simple_call.yaml
# a !sequence (e.g. the matrix) is dispatched cell by cell — each MCL cell is
# path-split, ANA/PDE cells are book-split by contract across the slaves:
./scripts/run_local_client_matrix.sh samples/matrix.yaml --port 8090
```

The master splits `paths` as evenly as possible (capping the fan-out at `paths`
so no slave gets zero) and gives each slave a distinct `seed` for the pseudo-
random generator plus an explicit Sobol skip (the running path count of the
slaves before it), so the slaves draw **disjoint, independent** blocks even when
the split is uneven. It pools every numeric field a slave reports
path-weighted — premium, the book Greeks (delta…theta), the model-parameter
`vega_<param>` Greeks and the per-contract premia/Greeks — and combines the
per-slave variances for the `*_trust` standard errors. Pooling is
schema-agnostic (it enumerates the result keys rather than matching a fixed
list), so a new result field aggregates without touching the master. The
pooling is exact: 2×100k slaves reproduce a single 200k-path run to machine
precision. An **ANA/PDE book** is instead **split by contract**: with ≥2 contracts
and ≥2 slaves, each slave prices a disjoint subset of the contracts (their
per-contract solves are independent — no cross-contract coupling, and the market
data + correlation are replicated to every slave), so the result is identical to a
single-box run. The per-contract result fields are unioned across slaves and the
book-level aggregates summed (premium, the book Greeks, the model-param
`vega_<param>`; `premium_trust` in quadrature), with `task_time` reported as the
slowest slave. This turns a strike×maturity PDE grid — one finite-difference solve
per cell — into a fan-out across the whole slave pool. A **`!sequence`** root is
dispatched task by task: each MCL cell is path-split and each ANA/PDE cell
book-split across the slaves in turn, and every cell's result block is gathered
into one response — so the full matrix gets the cluster applied cell by cell. The
remaining non-splittable roots (a GPU-MCL cell, which uses the master's device, or
a book too small to split) are computed by the **master itself** rather than
offloaded whole onto a slave.

While the slaves run, the master draws an **approximate global progress bar** by
polling each slave's `GET /progress` (the in-flight pricing's path/contract count)
and summing them — work done / work total across the cluster, as a percent. A slave
that fails to answer a poll simply drops out of that tick's total, so the bar
degrades gracefully rather than stalling. The master **re-exposes that aggregate**
on its own `GET /progress` (same `"<current> <total> <active>"` contract as a plain
`-server`), so a client leasing the master — e.g. the web BFF — reads progress
uniformly whether the work was path-split, contract-split or run on the master.

### Docker

```bash
docker build -t thoth .
docker run --rm -p 8080:8080 thoth               # HTTP server on 8080
```

---

## Configuration

YAML, as a flat namespace of named objects; each object declares its kind
through a YAML tag (`!mcl_pricer`, `!equity`, ...) and is referenced by name. The
tag sits on its own line with the object's fields below; sequences stay inline.
`root` names the object to execute. A minimal 1y ATM call (spot 100, vol 30%,
rate 8%, prices to ~15.7 — Black-Scholes 15.71):

```yaml
root: my_pricing

my_pricing: !pde_pricer   # engine = the tag: !mcl_pricer | !pde_pricer | !ana_pricer
  today: 2000-01-01
  book: my_book
  currency: eur
  pde_configuration: my_pde   # engine params: an !mcl_pricer names mcl_configuration
  correlation: my_correl       # instead; an !ana_pricer needs neither
  indicators: [premium, delta]
  result: my_result
  # debug_configuration: my_debug   # optional, see below

# engine parameters live in their own objects, referenced directly by the pricer
# (and shareable between pricers). The engine itself is chosen by the pricer's tag
# above, not here. (GPU: !mcl_pricer + allow_gpu in the mcl_configuration.)
my_mcl: !mcl_configuration
  max_day_step: 1
  min_day_step: -1
  paths: 100000           # 64-bit; may exceed 2^31 (e.g. billions, useful on the GPU)
  vol_year_step: 0.01     # variance sub-step (yr): on Heston/Bates books caps the
                          # diffusion step below max_day_step to cut Andersen-QE
                          # bias on long steps; no effect on plain GBM books or <=0
  use_sobol: true
  allow_gpu: false       # true -> run on a CUDA GPU when present + book supported, else CPU
my_pde: !pde_configuration
  vanilla_precision: high   # low|medium|high: sizes the 1-D grid AND the Heston/Bates ADI grid

# optional debug switches (referenced from the pricer via debug_configuration).
# generate_nodes_graph returns each MCL tree's Monte-Carlo node DAG as Graphviz
# .dot text in the result block (<tree>_mcl_graph), e.g. extract premium_mcl_graph
# and render with: dot -Tpng -o nodes.png
my_debug: !debug_configuration
  generate_nodes_graph: true

my_book: !book
  contracts: [my_call]
my_call: !vanilla
  underlying: my_eq
  premium_currency: eur
  strike: 100
  maturity: 2000-12-31
  type: call
  exercise: european

my_eq: !equity
  spot: 100
  volatility: my_vol
  currency: eur
my_vol: !bs_volatility
  volatility: 30
  calendar: my_calendar
eur: !currency
  rate: eur_rate
eur_rate: !yield_curve
  dates: [2000-01-01, 2010-01-01]
  values: [8, 8]
my_correl: !correlation_matrix
  underlyings: [my_eq]
  matrix: [1]
my_calendar: !simple_weighted_calendar
  non_working_days_weight: 1
```

Conventions: volatilities and curve values are in **percent** (`30` -> 0.30,
`8` -> 0.08); booleans use `true` / `false` (yes/no still parse); vectors and
matrices are flat number lists. Output YAML is emitted with fields in
alphabetical order (stable, diff-friendly).

`samples/` holds the runnable books: `simple_call.yaml` (the 1y ATM call above,
Black-Scholes ~15.71, with the node-graph debug switch enabled), `matrix.yaml`
— a `!sequence` running the full pricer/product matrix (vanilla european/american,
quanto, composite, basket / best-of / worst-of, continuous / discrete / knock-in
barriers, variance swap, Heston, Bates, SABR local-vol, GPU `allow_gpu` cells, and
the model-parameter `vega_<param>` Greeks, across PDE / MCL / ANA) in one process —
and `lsv.yaml` (the LSV calibration-quality demo above).

---

## Repository layout

```
src/             C++ sources (engine, instruments, market data, IO)
tests/           doctest regression suite
samples/         example YAML configs
docs/            design notes
Dockerfile       production image (multi-stage; CPU by default, GPU via build-args)
.devcontainer/   VS Code dev container
.vscode/         editor tasks + gdb debug configs
.github/workflows/ CI (build + ctest + clang-format gate)
CMakeLists.txt   build
scripts/         shell wrappers (run from the project root, e.g. ./scripts/format.sh):
  build_run.sh                build (optionally --gpu) and batch-price a book, node-graph dump on
  format.sh                   clang-format wrapper (--check for CI)
  run_docker_batch.sh         build the image and price one YAML in batch: <input.yaml> [output.yaml]
  run_docker_server.sh        build the image and serve over HTTP ([--gpu] CUDA/--gpus all; [--slaves N] master + N slave cluster)
  run_docker_common.sh        shared helpers (image build, input/output paths) — sourced, not run
  run_local_client_matrix.sh  POST a !sequence book to a running server and tabulate it: <input.yaml> [--port N] [--raw out.yaml]
```

---

## Notes & limitations

- **Hull-White hybrid scope**: a book with a `rate_model` currency must be
  single-currency (no FX factors, quanto or composite settlements), on
  **`bs_volatility` equities only**, and **European** (the LSM American pass
  discounts deterministically — rejected). The **ANA** engine additionally
  restricts to the mono-equity European vanilla (barriers / variance swaps have
  no deterministic-df closed form under stochastic rates and are rejected);
  MCL prices any supported European payoff pathwise. The **PDE** engine rejects
  the hybrid entirely (a 2-D (S, r) ADI grid is a TODO). Rho bumps the curves
  only — (a, σ_r) sensitivities are not produced.
- **Term-structured correlation** interpolates the pillar matrices with **constant
  FX vols** in the pivot-triangle algebra (the same constant-vol convention the
  triangle formulas already assume): the products corr×vol consumed by the quanto /
  composite drifts are linear in the instantaneous entries, so the running-average
  entries make those drifts exact integrals. One path is an approximation: a
  **composite used as a correlated leg** of a further quanto (the
  composite-vol / composite-correl nodes take a `sqrt` of the averaged entry,
  which is nonlinear in ρ) — exact for a constant matrix, second-order in the
  pillar spread otherwise. Basket **moment matching** pairs the
  `[0, T]`-averaged correlations with the components' total vols — exact for
  constant component vols, the usual ATM approximation otherwise. The
  spot/variance `<name>_var` entries must be pillar-independent (validated), and
  the leverage calibration / Heston QE keep reading that single scalar ρ.
- **American vanilla** exercise is supported by **both** the PDE pricer and the
  Monte-Carlo pricer (Longstaff-Schwartz least-squares MC, a lower bound on the
  true value). American basket / path-dependent payoffs are not yet covered. An
  American MC run logs under the `AMC` label (vs `MCL` for a plain European run).
- The `nominal` contract field is not wired for vanillas and barriers (their
  premiums are per unit); the `variance` (`notional`) and the
  `autocallable` (`nominal`, default 100) DO scale their payoffs by it.
- The HTTP server serialises pricing requests (single global engine state).
- Yield/repo/dividend curves interpolate across their pillars in **every** engine:
  the MCL diffusion now follows the full curve term structure (each rate/repo/
  dividend leg is a term-structured zero-rate node, and the spot diffuses at the
  per-step forward carry), so a sloped curve reaches the right MCL forward and the
  MCL/PDE/ANA prices agree on steep curves. A flat curve reduces exactly to the
  previous single-rate behaviour. The **American LSM pass discounts each exercise
  cashflow at the zero rate of its own date** (read off the premium-currency curve,
  parallel-bumped for rho), consistent with the term-structured diffusion, and the
  **PDE grid steps at the per-interval forward carry/discount rates** read off the
  full curves (the last step pinned to the exact maturity, so the telescoped step
  discounts reproduce the maturity df — European prices are unchanged by
  construction while American interim exercise sees the true curve dynamics; a
  flat curve keeps the assembled-once fast path). Both engines are pinned against
  an independent term-structure binomial oracle in
  `tests/test_term_structure.cpp` (they agree with it to <0.5% where the old
  flat-maturity-rate treatments were 3.5% / 13.5% off on a 2%→10% curve).
- Local volatility: the **MCL** engine diffuses a `sabr_volatility` surface as a
  full **Dupire local-vol** surface — the surface is sampled onto a
  per-diffusion-date log-spot grid and read along each path. Mono, **basket**
  (each component on its own local vol) and **composite** underlyings are covered
  (the composite's quanto drift correction uses the ATM vol, as ANA/PDE do).
  The **ANA** engine uses the implied vol at the option's strike, and the **PDE**
  now reads the **Dupire local vol per grid node and per time step** for a mono
  local-vol underlying (vanillas and barriers), so by the Dupire repricing
  property its European prices match ANA at **every strike**, not just ATM —
  pinned across strikes in `tests/test_features.cpp`. The **variance-swap PDE**
  keeps its Dupire local-variance source, matching the analytic static
  replication. `bs_volatility` (flat) is exact in every engine (and keeps the
  assembled-once fast grid path). The ATM vol still sizes the PDE domain and
  feeds the quanto drift correction.
- SABR wing treatment scope: the power-law tails make the surface butterfly-
  arbitrage-free **beyond the ±2.5-sigma cutoff** (pinned by a density-positivity
  test). At extreme `nu*sqrt(T)` (vol-of-vol ~1 on multi-year pillars) Hagan's
  expansion can also violate **inside** the liquid band, where no wing treatment
  applies — use shorter parameter pillars or a genuine stochastic-vol model
  (`heston_volatility`) there. Calendar (in-T) arbitrage is likewise out of scope;
  the Dupire local variance is backstopped **on both sides**: floored at a tiny
  positive value (the sqrt stays real when the wing arbitrage turns it slightly
  negative) and **capped at (5 × the node's implied vol)²** (the same degeneracy
  can collapse the Dupire denominator towards 0⁺ and blow the local variance up
  instead — the cap keeps those wing nodes from distorting the MCL/PDE diffusion;
  pinned on an extreme `nu·sqrt(T) ≈ 2.2` fixture in `tests/test_features.cpp`).
- LSV (`lsv_volatility`) scope: the leverage is calibrated by a **binned particle
  method** (fixed-seed 16k-path pre-pass, 30 log-spot bins), so the vanilla
  repricing carries a small calibration bias on top of the engines' own error
  (pinned to ~0.1 on a 1y spot-100 call in `tests/test_lsv.cpp`). ANA rejects the
  model (no closed form). Bates jumps under LSV are a configuration error. The
  PDE prices LSV **vanillas** through the leveraged 2-D ADI; an LSV **barrier**
  on the PDE falls back to the 1-D grid on the target surface's vol (like the
  Heston barrier PDE today), so prefer MCL for LSV path-dependents. The Greek
  bumps (vega and `vega_<param>`) **recalibrate the leverage per scenario** with
  common random numbers, so vega measures the target-surface sensitivity and the
  parameter vegas the pure smile-dynamics effect at a fixed vanilla surface.
- GPU (CUDA) acceleration (`mcl` + `allow_gpu`) currently covers **single-asset
  European vanillas under GBM** only; American / barrier / stochastic-vol /
  multi-asset books run on the CPU `mcl` engine. Extending the kernel to Heston-QE
  and multi-asset is future work — see [`docs/gpu.md`](docs/gpu.md).
