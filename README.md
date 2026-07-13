# Thoth — monorepo

[![CI](https://github.com/GValas/Thoth/actions/workflows/ci.yml/badge.svg)](https://github.com/GValas/Thoth/actions/workflows/ci.yml)

Thoth is an equity-derivatives pricing engine plus a web dashboard, kept in one
repository:

| Path | What it is |
|------|------------|
| [`pricer/`](pricer/README.md) | **The C++23 pricing engine** — Monte-Carlo, PDE and analytic pricers; multi-asset / multi-currency books; YAML-configured; usable as a batch tool or an HTTP service (`thoth -server`). This is the mature, fully-tested core. See **[`pricer/README.md`](pricer/README.md)**. |
| `web/` | **The web dashboard** — a NestJS BFF + Angular SPA over the engine: edit market data in an editable equities/rates/fx/correlation dashboard (with a random-sample generator), compute strike × maturity vanilla price grids (an option-chain view + Greeks), price single products in dedicated **vanilla / barrier / variance** panels, and monitor them live in a global **blotter**. The engine is used unmodified as a pricing microservice. `web/mcp/` additionally exposes the same engine to **LLM agents** as an MCP server (see below). |

## Engine quickstart

```bash
cmake -B pricer/build pricer && cmake --build pricer/build -j
ctest --test-dir pricer/build --output-on-failure
./pricer/build/thoth -batch pricer/samples/simple_call.yaml /tmp/out.yaml   # premium ~ 15.71
```

Full engine documentation — pricers, instruments, market data, the YAML format,
the HTTP server contract — is in [`pricer/README.md`](pricer/README.md).

## Web dashboard

Three tiers — Angular SPA → NestJS BFF → N engine endpoints. The prod stack wires those
endpoints as **2 GPU clusters**, each a `thoth -cluster` master (pinned to one GPU) that
prices a share on its GPU and fans the rest of an MCL book's paths out to **5 CPU
`thoth -server` slaves**:

```
Browser ─/api─▶ nginx+SPA ─▶ NestJS BFF ─┬─ BullMQ/Redis queue ─▶ EnginePool leases 1 of 2
 (Material/AG Grid)                       └─ SQLite (TypeORM)        cluster masters
                                                                     │ (GPU 0 / GPU 1)
                                                                     └─▶ 5 CPU slaves each
```

The BFF talks to each cluster master over the same HTTP API as a plain server (`POST
/price` with YAML, `GET /health`, `GET /progress`); a pricing job leases one cluster for
its duration, which makes that cluster's progress the job's progress. (The clusters need
the NVIDIA Container Toolkit + ≥1 GPU; by default both masters share GPU 0 — set
`THOTH_GPU_C2=1` to put the second cluster on a second card, and `THOTH_CUDA_ARCH` to the
GPUs' compute capability — see `docker-compose.yml`.) Two tabs: **Market Data** — a domain dashboard with
four editable areas (**Equities** spot/vol/repo/dividends · **Rates** · **FX** · **Correlation**
matrix), AG-Grid inline editing, full vol term-structures (flat/SABR/Heston/LSV — the
tabular kinds as editable pillar/parameter **tables**, LSV as the Heston parameter table
plus a **target-surface picker** over the workspace's deterministic vols). The book is a **fixed canonical set** — at most **5 stocks
+ 3 currencies (USD/EUR/JPY) + induced FX + correlation** — that you **edit in place** (no adding
or deleting items, fixed pillar sets); an empty workspace is **auto-seeded on first load**, and
**Generate sample data** (`POST /api/workspaces/:id/objects/seed`) reshuffles it. Each stock's
**repo curve and dividends share a single pillar table** (one row per pillar, a Repo and a
Dividend column) so the two curves always stay aligned. **Rates** is a single **shared-pillar
grid** (rows = pillars, one column per currency) so every currency curve always shares the same
pillar set. **FX** shows the pivot pairs (USD/EUR, USD/JPY) plus the **induced, triangulated
cross** (EUR/JPY = USD/JPY ÷ USD/EUR), arbitrage-free by construction; the pivot basis is keyed
off the pairs' shared underlying so it stays stable across reloads. The **Correlation** matrix is
always a valid (symmetric, unit-diagonal, **positive-definite**) matrix: the generator and every
hand edit project it onto the nearest valid correlation matrix (eigenvalue-clip repair), and the
member lists self-heal so they never reference a renamed object. Beyond the live equity **spots**,
the FX spots and the whole **correlation matrix tick in real time** off the `spot-feed` service
(SSE): FX pivot legs follow a GBM walk (the cross re-induces from them) and the correlation matrix
evolves under an Ornstein–Uhlenbeck mean-reversion with a per-tick positive-semidefinite repair so
it stays a valid correlation matrix; while Live is on those cells are read-only and tinted on each
move.
The other tab is **Vanilla Grid** (formerly "Pricing Grid"): pick the
engine (**ana / pde / mcl / mcl/gpu**), contract **currency**, underlyings, strikes, and
maturities (via a **date picker**); a first visit prepopulates a ready-to-price default grid
(strikes 80–120, the next five monthly maturities, EUR, European, Greeks on). Results render
as an **option chain** — one block per maturity (the maturity sub-tabs are labelled
`dd-MMM-yy`, e.g. `25-Jun-26`), **calls on the left and puts on the right of
a central strike column, strikes top-to-bottom**, each wing showing premium + the per-cell
Greeks — with a note of which **cluster/server** priced it and how long it took (engine
`task_time` + round-trip). The form, results and any in-flight job **persist across tab
navigation _and across a reconnection_** — the form is saved per workspace in `localStorage`
and the last run is re-fetched by id on reload (the `GridRun` lives server-side), reattaching
to the live job if it is still pricing. Per-cell Greeks come from **ana / pde / mcl/gpu**;
CPU **mcl** gives book-level Greeks only.
For **PDE/MCL** the grid builder synthesises a default engine-config object
(`!pde_configuration` at the **medium** precision preset / `!mcl_configuration`, with
`mcl/gpu` flipping `allow_gpu` on) and auto-attaches the workspace's correlation matrix when
the request carries none, so those engines price straight from the GUI.

Beyond the grid, the **Panels** tab prices a *single* hand-entered product with all its
variations across six sub-panels — **Vanilla** (call/put, European/American, strike,
maturity, nominal, absolute/relative strike), **Barrier** (the four `up&out / up&in /
down&out / down&in` types, continuous or discrete monitoring, barrier level, strike,
maturity), **Variance** (variance swap: volatility strike + notional) and
**Autocallable** (an **Athena** snowball or a **Phoenix** conditional-coupon note with
the **memory** variant — autocall / protection / coupon barriers as percent of spot, and
a generated observation schedule from a first date + monthly frequency + count; pde / mcl
only, no ANA closed form), **Asian** (arithmetic average-price call/put, absolute/relative
strike, averaging period), **Ratchet** (cliquet: per-period return clip [local floor,
cap] locked in, global floor/cap — both path-dependent, mcl only) and **Digital** (European
binary: cash-or-nothing / asset-or-nothing call/put, ana/pde/mcl) — each showing
premium + Greeks, each with a **Greeks** toggle that requests delta/gamma/vega/rho/theta
from the engine's bump pass — meaningful even on the path-dependent notes (e.g. vega/rho/
theta dominate on variance swaps and ratchets, where delta/gamma are ~0). Each panel can
**re-price live** off the spot feed on a throttle. **Every option priced in a panel is
systematically mirrored to the blotter** as a single evolving line (the sales workflow starts
there, not on an explicit send): the row appears the moment pricing starts and updates in place
on each re-price. A **Send to blotter** button remains (now idempotent — it updates the same
mirrored line rather than duplicating it), alongside a
**Termsheet** button that renders the product's booked description as a Markdown
document (the engine's `!termsheet` documentation task, via `POST
/api/instrument/termsheet`) and downloads it as a `.md` file — same form state as
the Price button, no pricing involved. The **Blotter** tab is
a global monitoring book: every product sent from a panel becomes a row showing its underlying's
**live spot** (next to the underlying), a premium re-priced for the whole book in **live
mode** (throttled, off the live spots), tinted green/red on each move, and the **wall-clock time
it was last priced** (also shown on each panel's status line); rows survive
tab navigation and a reload (persisted in `localStorage`). On a **fresh install**
the blotter self-seeds **10 random sample contracts** (vanillas / barriers / variance
swaps / Asians / ratchets on the workspace's **equity** underlyings — where every kind/engine
combo has a griddable, diffusable underlying; barrier levels are booked as absolute cash off
each equity's spot — each on an engine that can price it) so it opens on a live book rather than a blank
tab — seeded once per session whenever the blotter is empty (an **in-memory** guard, so a
stale flag can never permanently suppress it; once seeded the rows persist in `localStorage`).
Each row has a **tick box** (plus a header select-all) driving the **Re-price** toolbar action
(the ticked rows, or the whole book when none are ticked), and its own **Termsheet** button
that downloads that single product's termsheet as a Markdown file. **Double-clicking a row opens
it in its pricing panel** — the Panels tab jumps to the matching sub-panel (Vanilla / Barrier /
Variance / Autocallable / Asian / Ratchet / Digital) with the whole form prefilled from the booked
contract, ready to tweak and re-price. Every column **sorts** (click the header — asc / desc /
off) and carries a **per-column text filter** in its header, so the book can be ordered by any
field and narrowed live; the data columns can be **reordered by dragging their headers** (a drag
handle on each), premia/Greeks are shown to **2 decimals**, and the **Greek columns are foldable**
(hidden by default via a toolbar **Greeks** toggle for a lighter monitoring view). Each row carries
a **sales-workflow status** badge — `new` (created, not yet priced) → `quoting` (being priced by
the engine) → `quoted` (priced, awaiting the sales decision) → the salesperson resolves it from the
**action column** to a frozen terminal state, `traded` (dealt on behalf of the client) or `missed`
(client dealt elsewhere); terminal rows are never re-priced (their executed quote is frozen and
survives a reload), and an **undo** action reopens a mis-marked row. Only the **sales** role is
modelled for now (trader / client roles come later). Pricing is backed by a synchronous
`POST /api/instrument/price` endpoint (`live: true` overlays the live spots **and the live
correlation matrix** — each streamed pair takes its live value, the rest keep their stored
one, and the blend is Cholesky-gated back to the stored matrix if mixing would break
positive-definiteness) that builds a one-contract book — the single-product counterpart of
the grid pipeline; the grid's live mode applies the same two overlays, so the priced market
always matches what the Market Data screen shows. Validation flags non-SPD correlations and
non-increasing curve dates **before** pricing (both are engine hard-rejects), and the
correlation repair floors eigenvalues high enough (1e-3) that the 1e-4 payload rounding can
never push a repaired matrix back to indefinite.
Auth is JWT + rotating refresh cookie with an admin RBAC tab; the stored refresh-token hash
is a bcrypt of the token's **sha256 digest** (bcrypt alone reads only the first 72 bytes,
which are identical across a user's JWTs — hashing the digest keeps rotation/replay
detection effective). The BFF is **security-hardened**: it **refuses to boot** unless
`JWT_SECRET`/`JWT_REFRESH_SECRET` are present, ≥32 chars and not a placeholder (no hardcoded
fallback); every workspace and all workspace-derived data (market objects, grids, single
pricings, termsheets) are **owner-scoped** (a workspace/run that is not yours reads back as a
404, not a 403, so ids can't be enumerated — admins bypass); `@nestjs/throttler` rate-limits
the API globally and **login/refresh strictly** (5/min per IP); a **1 MB JSON body limit**,
grid **dimension caps** (≤200 strikes / 120 maturities / 20 underlyings / 8 types and
≤50 000 total cells), **helmet** security headers and a **prod-gated Swagger** round it out.
Free-form instrument/market-data fields are stripped of reserved keys (`__proto__`,
`constructor`, `prototype`, `__tag`) and the validated kind tag is always written last, so a
crafted `__tag` can't override the priced product.

**Look & feel:** the whole SPA shares a dense, finance-grade charte driven by a single
design-token layer (`web/frontend/src/styles/_tokens.scss` — palette, a px **type scale**,
a 4px **spacing scale**, and **control-size** dials — consumed as CSS custom properties),
with Angular Material run at density `-4` and a tightened AG-Grid quartz theme for compact,
trading-desk rows. Two interchangeable themes are built from the same tokens: a **refined
light** charte (default) and a **Bloomberg-style dark terminal** charte (true-black canvas,
amber accent + amber column headers, monospace numerics, brighter green/red direction
colours). A toolbar toggle (`ThemeService`, persisted in `localStorage`) flips between them
at runtime by toggling a `theme-dark` class; the heatmap and grids re-skin automatically by
reading the active theme's CSS variables.

**Run (prod, Docker):**

```bash
cp .env.example .env          # then edit the secrets (JWT_*, ADMIN_PASSWORD)
scripts/prod.sh               # build + start, wait for health, then STAY ATTACHED
# open http://localhost:7777  (login with ADMIN_EMAIL / ADMIN_PASSWORD)
# Ctrl-C here tears the whole stack down in cascade; scripts/prod.sh logs|ps|down to manage it
```

`scripts/prod.sh` wraps `docker compose` (`docker-compose.yml`: redis · 2 GPU clusters ·
bff · web); it refuses to start while `.env` still holds `change-me` placeholder
secrets (override with `FORCE=1`). After the health check it **holds the foreground**,
streaming every service's logs; `Ctrl-C` — or any container exiting/crashing — runs a
cascade `docker compose down` (pass `DETACH=1` for the old detached behaviour). You can also drive compose directly. The
`web/.dockerignore` and `web/frontend/.dockerignore` files keep host-built artefacts
(`node_modules`, `dist`, `*.tsbuildinfo`) out of the build contexts, so the in-container
installs/builds are authoritative — without this a stale `tsbuildinfo` makes the BFF's
incremental `tsc` emit nothing and the image build fails on the missing `dist`.

**Develop:** the default devcontainer carries the C++ toolchain *and* Node, so you can
run all three locally — `thoth -server 8080`, then in `web/bff` `npm i && npm run start:dev`
(memory queue, no Redis needed), then in `web/frontend` `npm i && npm start` (proxies
`/api` → `:3000`). An opt-in 3-container dev stack is in
[`.devcontainer/docker-compose.dev.yml`](.devcontainer/docker-compose.dev.yml). On open the
container auto-launches the Claude CLI in a dedicated terminal (a `folderOpen` task in
[`.vscode/tasks.json`](.vscode/tasks.json), bypass-permissions mode). BFF API
docs are served at `/api/docs`. Design notes: [`todo-gui.md`](todo-gui.md).

## MCP server (agent access to the engine)

`web/mcp/` wraps the running `thoth -server` as a **Model Context Protocol** server
(stdio), so MCP clients — Claude Code, Claude Desktop, any agent framework — can
price with the C++ engine as ordinary tools. It reuses `@thoth/shared` (the same
engine client / YAML builders the BFF uses) and reads the engine's JSON schema, so
it stays in lock-step with the config format.

Tools: `price_vanilla` (european/american, flat vol **or a SABR smile**, optional
Greeks), `price_barrier` (the four knock types, continuous or discrete monitoring),
`price_variance_swap` (incl. discrete fixing schedules), **`price_yaml_book`** (raw
YAML pass-through — the full engine: Heston/Bates, quanto/composite/basket,
`!sequence` matrices, `vega_<param>` Greeks), `get_config_schema` (author configs
from the live schema) and `engine_health` (reachability + latency; the internal
engine URL is not exposed).

The tools are **resource-bounded** so an agent cannot monopolise the cluster: MCL
`paths` ≤ 1,000,000 and SABR `maturities` ≤ 20 pillars on the synthesized-book
tools; raw `price_yaml_book` configs are capped at 256 KB, at most 50 tasks per
`!sequence` and 1,000,000 paths per `mcl_configuration`. At most **2 MCP pricing
requests** run against the engine concurrently (the same `EnginePool` gate the BFF
uses); further requests queue, so agent bursts cannot starve the dashboard.

```bash
cd web/shared && npm install && npm run build     # shared engine client
cd ../mcp    && npm install && npm run build      # -> web/mcp/dist/index.js
```

The repo ships a project-scoped **`.mcp.json`**, so Claude Code picks the server up
automatically. It connects to `THOTH_ENGINE_URL` (default `localhost:8080`); when
`THOTH_ENGINE_BIN` is set (as `.mcp.json` does, pointing at `pricer/build/thoth`)
and nothing answers on that URL, the MCP server **spawns the engine itself** and
tears it down on exit — a fully self-contained setup once the engine is built. For
Claude Desktop, register `node <repo>/web/mcp/dist/index.js` with the same env.

Two transports: **stdio** (default — launched by the MCP client, as above) and
**Streamable HTTP** (`MCP_TRANSPORT=http`, stateless, `POST /mcp` + a `GET /healthz`
probe). The prod stack runs the HTTP mode as the **`mcp` compose service**, wrapping
cluster 1's master (so agent MCL books get path-split across its slaves like any
dashboard job). It has **no host port of its own**: nginx (the `web` service) reverse-
proxies it, so it shares the dashboard's public port at `http://localhost:7777/mcp`
(health at `/mcp/healthz`). `scripts/prod.sh` waits for its health and prints the
ready-to-paste registration.

**Authentication (HTTP mode):** set **`MCP_API_KEY`** in the root `.env` (see
`.env.example`; compose passes it to the `mcp` service) and every `POST /mcp` must
then carry `Authorization: Bearer <key>` — requests without it get a 401
(constant-time key compare; nginx forwards the header untouched). When the variable
is unset the endpoint is **unauthenticated** and the server logs a prominent startup
warning — acceptable only on trusted networks. Stdio mode is unaffected (the MCP
client owns the process).

```bash
claude mcp add --transport http thoth-pricing http://localhost:7777/mcp \
  --header "Authorization: Bearer $MCP_API_KEY"
```

## Repository map

```
Thoth/
├── README.md            # this file (monorepo map)
├── CLAUDE.md            # agent mandate (build/test, README sync)
├── docker-compose.yml   # prod: 2 GPU clusters (master+5 slaves) · redis · bff · web · mcp
├── .env.example         # compose secrets (JWT, admin seed)
├── pricer/              # C++ engine (CMake, src/ tests/ samples/ schema/ docs/ scripts/)
├── web/
│   ├── shared/          # @thoth/shared: engine client, pool, tag-YAML, grid + instrument builders
│   ├── bff/             # NestJS BFF (auth, workspaces, marketdata, schema, grid, instrument, live spots)
│   ├── spot-feed/       # fake real-time feed: GBM equity+fx spots & an OU correlation matrix -> Redis pub/sub
│   ├── mcp/             # @thoth/mcp: MCP server (stdio + HTTP) exposing the engine as agent tools
│   └── frontend/        # Angular SPA (Material + AG Grid + ngx-formly): Vanilla Grid · Panels · Blotter
├── .github/             # CI (runs the engine gates under pricer/)
├── .devcontainer/       # single-container dev (+ opt-in 3-container compose), .vscode/, .claude/
└── todo-gui.md          # web dashboard design notes
```
