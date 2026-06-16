# Thoth

A C++23 pricing engine for equity derivatives — Monte-Carlo, PDE and analytic
pricers, multi-asset / multi-currency books, driven by a YAML configuration,
usable as a batch tool or as an HTTP service.

---

## Features

**Pricers** (selected per book via `method`)
- **Monte-Carlo (`mcl`)** — correlated geometric Brownian motion via a Cholesky
  factorisation of the correlation matrix; exact log-Euler step for constant
  volatility (no discretisation bias). With `use_sobol: true` the increments are
  drawn from a **Sobol** low-discrepancy sequence laid out by a **Brownian
  bridge** (the coarsest, most important path structure gets the lowest Sobol
  dimensions, the rest pseudo-random) — far faster convergence on smooth payoffs
  (e.g. a 1y ATM call at 8k paths: pseudo error ~0.8, Sobol ~0.01). The Sobol
  sequence uses the **Joe-Kuo** direction numbers (`new-joe-kuo-6.21201`,
  embedded), so the low-discrepancy treatment extends to several thousand
  dimensions rather than GSL's 40 — multi-asset books and finely-stepped
  path-dependent payoffs keep the QMC benefit. Reproducible: the node graph and
  factors are name-ordered, so results are independent of heap layout / build /
  platform.
- **PDE (`pde`)** — Crank-Nicolson finite-difference scheme; supports **American**
  exercise (backward induction `max(intrinsic, continuation)`).
- **Analytic (`ana`)** — closed-form (Black-Scholes) for instruments that admit
  it.

Long runs show a `│███░░░│` progress bar with the running price/trust. On a
terminal it redraws in place as a single updating line; when stdout is not a TTY
(output redirected, or captured via `docker logs`) it falls back to one bar line
every 10% so logs stay readable instead of piling up the redraw frames. A pricer
can attach a `debug_configuration` with `generate_nodes_graph: true` to dump the
Monte-Carlo node graph as a Graphviz `.dot` file for inspection.

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
  per bump. theta is a separate one-day reprice. Sharing the path nodes is exact
  common-random-numbers, so the Greeks match the old reset-per-scenario values
  bit-for-bit. Books with American contracts or non-trivial (basket/composite)
  underlyings fall back to book-level bump-and-revalue.

Units: delta `dP/dS`, gamma `d²P/dS²`, vega per vol point, rho per 1% parallel
rate move, theta per calendar day. On a 1y ATM call the three engines agree and
sit close to Black-Scholes (delta ≈0.67, gamma ≈0.012, vega ≈0.37, rho ≈0.50,
theta ≈−0.026 — delta carries the small one-sided-bump bias ½·gamma·S·ε).

**Instruments**
- `vanilla` — call / put, **european** or **american**, absolute or
  relative strike. **Quanto** (a foreign-currency asset paid in the book
  currency) is supported by all three engines — the drift correction
  `F *= exp(-ρ·σ_S·σ_X·t)` lives in the MCL node graph, ANA's quanto forward and
  the PDE carry, and the three agree (American quanto via PDE / MCL).
- `barrier` — knock-out / digital payoffs, **continuous or
  discrete monitoring** (`monitoring_period_days`); PDE, MCL and (continuous-only)
  closed-form.
- `variance_swap` — pays `notional * (realized_variance - strike_variance)`;
  priced by **Monte-Carlo** (realized variance of the simulated path) or
  **analytically** by static replication (the 1/K²-weighted option strip, so a
  smile feeds in).

**Underlyings**
- `equity`, `composite` (compo / quanto), `basket`, plus `currency` /
  `forex` for FX.

**Market data**
- `yield_curve`, `repo_curve`, `continuous_dividends_curve`,
  `correlation_matrix`.
- Volatilities: `bs_volatility` (flat Black-Scholes vol), `sabr_volatility`
  (Hagan 2002 lognormal SABR implied surface, per-maturity `alpha`/`beta`/`rho`/`nu`)
  and `heston_volatility` (genuine stochastic vol — see below).

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
on the QE spot) and **ANA** (the closed-form jump characteristic function
multiplies the Heston CF); the PDE grid stays pure Heston (no jump term). See
`samples/bates_call.yaml`.

**Analytics objects**
- `pricer`, `historical_volatility_computation`,
  `historical_correlation_computation`.
- `sequence` — a task that runs a list of other tasks in order, each writing its
  own result block (e.g. price a whole book of cases in one process).

---

## Build

Dependencies (Debian / Ubuntu):

```bash
sudo apt-get install -y build-essential cmake pkg-config \
    libgsl-dev libboost-all-dev libyaml-cpp-dev libcpp-httplib-dev
```

| Library      | Use                              |
|--------------|----------------------------------|
| GSL          | linear algebra, RNG, statistics  |
| Boost        | date handling (header-only here) |
| yaml-cpp     | configuration format             |
| cpp-httplib  | HTTP server / client             |

```bash
cmake -B build
cmake --build build -j        # -> ./build/thoth
```

A devcontainer (`.devcontainer/`) and a production `Dockerfile` (multi-stage,
lean runtime image) are provided. The devcontainer preinstalls the C++ toolchain
extensions plus **Graphviz Interactive Preview** (`tintinweb.graphviz-interactive-preview`),
so the `generate_nodes_graph` `.dot` dumps can be previewed in-editor without an
external `dot` render.

### Tests

A doctest suite (`tests/`) covers European/American vanillas, barriers,
dividends, multi-asset consistency, engine-vs-engine agreement (incl. quanto),
variance swaps, baskets, composites, the SABR surface, Sobol QMC, the `!sequence`
task, the historical vol/correlation analytics, determinism and config parsing
(~71% line coverage). It is built alongside the binary:

```bash
cmake --build build -j           # builds thoth + thoth_tests
ctest --test-dir build --output-on-failure
```

### Formatting

`./format.sh` runs clang-format (style in `.clang-format`) over `src/` and
`tests/`; `./format.sh --check` fails on unformatted files (CI gate).

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
./build/thoth -batch <input.yaml> <output.yaml> [exec_name]
# or build the production image and price in a container — the result is written
# next to the input as <input>.out.yaml, owned by the invoking user:
./run_docker_batch.sh --input samples/simple_call.yaml
```

The output YAML is the input config with the computed `*_result` blocks added.

To exercise every engine at once, `samples/matrix.yaml` is a `!sequence`
task that runs the full pricer/product matrix (vanilla european/american,
continuous/discrete barriers, variance swap, across PDE / MCL / ANA) in one
process — price it like any other book (`-batch`, or post it to a server with
`run_local_client.sh`).

### HTTP pricing service

```bash
./build/thoth -server 8080                       # POST /price, GET /health
# or build the image and serve from a container:
./run_docker_server.sh --port 8080

curl --data-binary @samples/simple_call.yaml localhost:8080/price
# the built-in client:
./build/thoth -client http://localhost:8080 samples/simple_call.yaml
# or the wrapper, which POSTs the input to a running server and writes the result
# next to it as samples/<input>.out.yaml (these *.out.yaml files are gitignored):
./run_local_client.sh --input samples/simple_call.yaml --port 8080
```

`POST /price` takes a YAML body and returns the YAML result; an optional
`X-Exec-Name` header selects which object to run (default: the `root` object).
Send `Content-Type: application/x-yaml` for bodies over ~8 KB (`run_local_client.sh`
already does); the default form content type is capped by the HTTP library.

Requests are serialised (the engine shares global GSL state): the server logs
the client IP on arrival and again if a request has to wait. If a client
disconnects mid-pricing the run is cancelled, freeing the server instead of
finishing a result nobody will read. `run_docker_server.sh` passes `docker run -t`, so
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
# or the Docker wrapper: one container per slave + a master, all on a private
# network (Ctrl-C, or the master exiting, tears the whole cluster down):
./run_docker_cluster.sh --port 8090 --slaves 2
# then POST a book to the master (single-MCL-pricer books get path-split):
./run_local_client.sh --input samples/simple_call.yaml --port 8090
```

The master splits `paths` evenly and gives each slave a distinct `seed`, which
offsets the Sobol sequence by `seed * paths` (and seeds the pseudo-random
generator), so the slaves draw **disjoint, independent** blocks. It pools the
premium path-weighted and combines the per-slave variances for the trust. The
pooling is exact: 2×100k slaves reproduce a single 200k-path run to machine
precision. Only an MCL single-pricer is split; any other root is forwarded whole
to one slave.

### Docker

```bash
docker build -t thoth .
docker run --rm -p 8080:8080 thoth               # HTTP server on 8080
```

---

## Configuration

YAML, as a flat namespace of named objects; each object declares its kind
through a YAML tag (`!pricer`, `!equity`, ...) and is referenced by name. The
tag sits on its own line with the object's fields below; sequences stay inline.
`root` names the object to execute. A minimal 1y ATM call (spot 100, vol 30%,
rate 8%, prices to ~15.7 — Black-Scholes 15.71):

```yaml
root: my_pricing

my_pricing: !pricer
  today: 2000-01-01
  book: my_book
  currency: eur
  configuration: my_config
  correlation: my_correl
  indicators: [premium, delta]
  result: my_result
  # debug_configuration: my_debug   # optional, see below

# engine parameters live in their own referenced objects: the
# pricer_configuration selects the method and points at an mcl_configuration
# and/or pde_configuration sub-object.
my_config: !pricer_configuration
  method: pde   # pde | mcl | ana
  mcl_configuration: my_mcl
  pde_configuration: my_pde
my_mcl: !mcl_configuration
  max_time_step: 1
  min_time_step: -1
  paths: 50000
  vol_time_step: 0.01
  use_sobol: true
  use_milstein: true
my_pde: !pde_configuration
  vanilla_precision: high

# optional debug switches (referenced from the pricer via debug_configuration).
# generate_nodes_graph dumps the Monte-Carlo node DAG to <log_path><pricer>_nodes.dot
# (Graphviz), e.g. render with: dot -Tpng my_pricing_nodes.dot -o nodes.png
my_debug: !debug_configuration
  generate_nodes_graph: true

my_book: !book
  options: [my_call]
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

`samples/` holds runnable books: `simple_call.yaml` (the 1y ATM call above,
Black-Scholes ~15.71, with the node-graph debug switch enabled),
`heston_call.yaml` (a Heston call priced by all three engines),
`bates_call.yaml` (a Bates — Heston + jumps — call priced by MCL, dumping its
node DAG to Graphviz) and `matrix.yaml` — a `!sequence` running the full
pricer/product matrix (vanilla european/american, quanto, continuous/discrete
barriers, variance swap, across PDE / MCL / ANA) in one process.

---

## Repository layout

```
src/             C++ sources (engine, instruments, market data, IO)
tests/           doctest regression suite
samples/         example YAML configs
Dockerfile       production image (multi-stage)
.devcontainer/   VS Code dev container
.vscode/         editor tasks + gdb debug configs
CMakeLists.txt   build
format.sh             clang-format wrapper (--check for CI)
run_docker_batch.sh   build the image and price one YAML in batch (in a container)
run_docker_server.sh  build the image and run the HTTP pricing server (container)
run_docker_cluster.sh build the image and bring up a master + N slave containers
run_docker_common.sh  shared helper (builds the single Thoth image) — sourced, not run
run_local_client.sh   POST one YAML to a running server, write <input>.out.yaml
```

---

## Notes & limitations

- **American vanilla** exercise is supported by **both** the PDE pricer and the
  Monte-Carlo pricer (Longstaff-Schwartz least-squares MC, a lower bound on the
  true value). American basket / path-dependent payoffs are not yet covered.
- The `nominal` contract field is not yet wired into the engine (premiums are
  per unit).
- The HTTP server serialises pricing requests (single global engine state).
- Pricing currently uses a single reference (ATM) volatility per underlying, so
  a `sabr_volatility` smile only influences the price through its ATM level; a
  full local-vol grid is not wired in. `bs_volatility` (flat) is exact.
