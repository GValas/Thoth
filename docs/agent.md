# Working on Thoth as the IT Quant Agent

This guide is for any AI agent (or new contributor) working in the Thoth
repository. It is derived from [`CLAUDE.md`](../CLAUDE.md) (the agent mandate)
and the actual repo workflow. When this guide and `CLAUDE.md` disagree,
`CLAUDE.md` wins.

Repo root: `/workspaces/Thoth`.

---

## 1. MANDATORY: keep `README.md` in sync before every commit/push

This is the one non-negotiable rule. Quoting `CLAUDE.md` directly:

> **Before any `git commit` or `git push`, update `README.md` so it reflects the
> change being committed.** This is non-negotiable: the README is the single place
> where the project's evolutions are followed, so it must never lag the code.

Before committing/pushing, check whether the change touches anything the README
documents and update the matching section. The triggers, verbatim from
`CLAUDE.md`:

- a new/changed/removed **pricer, instrument, underlying, market-data or
  volatility kind** → *Features*
- a new/renamed/removed **run wrapper or CLI mode** → *Usage* and *Repository layout*
- a new/changed **sample** in `samples/` → the `samples/` paragraph in *Configuration*
- a new **YAML field / configuration switch** → *Configuration* (and the example if relevant)
- a new **build/test/tooling** step or dependency → *Build*
- a known **limitation** added or lifted → *Notes & limitations*

> If a change genuinely needs no README edit (pure internal refactor, formatting,
> a bug fix with no user-visible or documented behaviour change), that is allowed —
> but say so explicitly in the commit so the decision is visible. When in doubt,
> update the README.

So: when no README edit is needed, **say so explicitly in the commit message**
(e.g. "No README change: pure internal refactor"). The README sync is part of
the *same* commit as the change — never a follow-up.

---

## 2. Build & test workflow

The canonical commands (from `CLAUDE.md` and `README.md`):

```bash
cmake -B build && cmake --build build -j      # -> ./build/thoth (+ thoth_tests)
ctest --test-dir build --output-on-failure    # doctest suite (tests/)
./format.sh                                    # clang-format over src/ and tests/
```

- **Build** produces `./build/thoth` and the `thoth_tests` binary in one pass.
- **Tests** are a [doctest](https://github.com/doctest/doctest) suite in
  `tests/`, built alongside the binary. They cover european/american vanillas,
  barriers, dividends, multi-asset and engine-vs-engine agreement, variance
  swaps, baskets, composites, the SABR surface, Sobol QMC, the `!sequence` task,
  determinism and config parsing. Run them with `ctest` after every change.
- **Formatting** is enforced. `./format.sh` formats in place;
  `./format.sh --check` reports unformatted files and is **the CI gate** — run
  it before committing. The style lives in `.clang-format` (Allman braces,
  `ColumnLimit 0`). `clang-format` is preinstalled in the devcontainer.

A GPU (CUDA) build is opt-in and off by default; see `docs/gpu.md`. A Debug build
goes to `build-debug/` via the VS Code F5 task and
leaves the Release `build/` untouched.

Always build, test and format before you consider a change done.

---

## 3. Repository layout

```
src/             C++ engine, instruments, market data, IO
  core/          object model: Object, ObjectManager, object_registry, collectors
  tasks/         pricers (mcl/pde/ana/mcl_gpu), pricer & mcl/pde configurations, sequence
  contracts/     instruments: book, contract, vanilla, barrier, variance_swap
  underlyings/   mono, basket, absolute_basket, rainbow, composite, underlying
  marketdata/    equity, currency, curves (yield/repo/dividends), forex, asset
  vol_correl/    volatilities (bs/sabr/heston) + correlation
  nodes/         Monte-Carlo node graph (brownian, drift, heston, jump, local-vol, ...)
  helpers/       linalg (la_*), maths, distributions, statistics, rng, progress_bar, raii
  fixings/       fixing data
tests/           doctest regression suite
samples/         runnable YAML books (simple_call, matrix)
docs/            design notes & guides (this file lives here)
CMakeLists.txt   build
format.sh        clang-format wrapper (--check is the CI gate)
run_docker_*.sh  Docker batch/server wrappers; run_local_client_matrix.sh posts a !sequence + tabulates
```

See `README.md` *Repository layout* for the full wrapper-by-wrapper table.

---

## 4. Conventions observed in the codebase

Match these — don't reformat or rename surrounding code to a different style.

- **Naming.** Data members carry a leading underscore (`_strike`,
  `_maturity_date`, `_method`). Methods are PascalCase with **no** trailing
  underscore (`SetStrike`, `GetMaturityDate`, `ANA_EvalPrice`). Engine-specific
  hooks are prefixed by engine (`PDE_HasSolution`, `ANA_EvalPrice`,
  `GPU_GbmParams`). Kind tags are `KIND_*` constants in `enums.hpp`.
- **Object-registry + YAML config model.** Every priceable thing is a named
  `Object`. The YAML is a flat namespace of named objects, each tagged by kind
  (`!pricer`, `!equity`, `!vanilla`, ...) and referenced by name; `root` names
  the object to execute. `src/core/object_registry.cpp` is *the* single
  translation unit aware of every concrete type: it builds a `kind -> factory`
  table, one entry per object kind. Adding a type = a new class + exactly one
  entry there; `ObjectManager` stays type-agnostic.
- **Node-graph MCL design.** The Monte-Carlo engine builds a DAG of nodes
  (`src/nodes/`) — brownian/noise, drift, spot, local-vol, heston, jump,
  contract/flow nodes — that share expensive path nodes across base + Greek-bump
  sub-trees and price them in a single path sweep. The graph and factors are
  name-ordered so results are deterministic across platforms/builds. A pricer
  can dump the DAG to Graphviz via a `debug_configuration` with
  `generate_nodes_graph: true`.
- **RAII linalg wrappers (`la_*`).** `src/helpers/linalg.hpp` provides
  `la_vector` / `la_matrix` with a GSL-style heap-alloc/free C API
  (`la_vector_alloc`, `la_matrix_free`, ...). They are the in-repo replacement
  for the dropped GSL dependency; copying is deliberately disabled, so always
  pass them by pointer.
- **Conventions in config.** Vols and curve values are in **percent** (`30` →
  0.30); booleans are `true`/`false`; vectors/matrices are flat number lists;
  output YAML fields are emitted alphabetically (diff-friendly).
- **Commit messages.** End every commit with the trailer:

  ```
  Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
  ```

  Commit bodies in this repo are descriptive: summary line, a short paragraph,
  then bullets of what changed, and a note of what was verified.

---

## 5. Commit / push discipline

- **Only commit or push when the user asks.** Doing the work is not a license to
  commit it.
- If you are on the default branch (`main`) and need to commit substantial work,
  **branch off `main` first**.
- The **README sync from §1 is part of the same commit** as the change. Update
  it (or explicitly note no edit was needed) before you run `git commit`.
- Use the `Co-Authored-By` trailer above on every commit.
- Use the `gh` CLI for any GitHub operations (PRs, issues).

---

## 6. "Adding a feature" checklist

Example: adding a new product (e.g. a new instrument) or a new volatility kind.

1. **Implement** the class under the right `src/` subdirectory (`contracts/`,
   `underlyings/`, `vol_correl/`, `marketdata/`, `tasks/`, ...). Follow the
   naming and member conventions in §4. For a new instrument, implement the
   engine hooks it supports (`GetFlowNode` for MCL, `PDE_*`, `ANA_*`,
   `GPU_GbmParams`).
2. **Register it** in `src/core/object_registry.cpp`: add one
   `KIND_* -> factory` entry that reads the object's YAML fields and wires
   dependencies. Add the `KIND_*` tag and any parse helpers in `enums.hpp`.
   Add the new header to the include block at the top of `object_registry.cpp`.
3. **Add a doctest** in `tests/` — ideally an engine-vs-engine agreement or a
   closed-form sanity check, plus config-parsing coverage.
4. **Add a runnable sample** in `samples/` if it is a user-facing kind, so it
   can be priced end-to-end.
5. **Update `README.md`** per the §1 triggers (Features / Configuration / etc.)
   **and** the relevant `docs/` guide (products, volatility, monte_carlo, pde,
   gpu, ...).
6. **Build, test, format**:

   ```bash
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   ./format.sh --check
   ```

7. **Commit** only when asked, on a branch if needed, with the README synced and
   the `Co-Authored-By` trailer.

---

## See also

- [`running.md`](running.md) — running the engine (batch / server / cluster)
- [`monte_carlo.md`](monte_carlo.md) — the MCL node-graph engine
- [`pde.md`](pde.md) — the Crank-Nicolson / ADI PDE engine
- [`products.md`](products.md) — instruments and payoffs
- [`volatility.md`](volatility.md) — volatility surfaces and stochastic vol
- [`gpu.md`](gpu.md) — the CUDA `mcl_gpu` backend
