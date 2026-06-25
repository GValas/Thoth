# Thoth — monorepo

[![CI](https://github.com/GValas/Thoth/actions/workflows/ci.yml/badge.svg)](https://github.com/GValas/Thoth/actions/workflows/ci.yml)

Thoth is an equity-derivatives pricing engine plus a web dashboard, kept in one
repository:

| Path | What it is |
|------|------------|
| [`pricer/`](pricer/README.md) | **The C++23 pricing engine** — Monte-Carlo, PDE and analytic pricers; multi-asset / multi-currency books; YAML-configured; usable as a batch tool or an HTTP service (`thoth -server`). This is the mature, fully-tested core. See **[`pricer/README.md`](pricer/README.md)**. |
| `web/` | **The web dashboard** — a NestJS BFF + Angular SPA over the engine: edit market data in an editable equities/rates/fx/correlation dashboard (with a random-sample generator), and compute strike × maturity price grids (an option-chain view + Greeks). The engine is used unmodified as a pricing microservice. |

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
four editable areas (**Equities** spot/vol/repo/dividends · **Rates** per-currency yield
curves · **FX** pairs · **Correlation** matrix), AG-Grid inline editing, full vol
term-structures (flat/SABR/Heston), an **Advanced** schema-driven editor for other object
kinds, and a **Generate sample data** button (`POST /api/workspaces/:id/objects/seed`,
default 5 equities with realistic tickers / 3 currencies) — and **Pricing Grid**: pick the
engine (**ana / pde / mcl / mcl/gpu**), contract **currency**, underlyings, strikes, and
maturities (via a **date picker**); a first visit prepopulates a ready-to-price default grid
(strikes 80–120, the next five monthly maturities, EUR, European, Greeks on). Results render
as an **option chain** — one block per maturity, **calls on the left and puts on the right of
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
Auth is JWT + rotating refresh cookie with an admin RBAC tab.

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
[`.devcontainer/docker-compose.dev.yml`](.devcontainer/docker-compose.dev.yml). BFF API
docs are served at `/api/docs`. Design notes: [`todo-gui.md`](todo-gui.md).

## Repository map

```
Thoth/
├── README.md            # this file (monorepo map)
├── CLAUDE.md            # agent mandate (build/test, README sync)
├── docker-compose.yml   # prod: 2 GPU clusters (master+5 slaves) · redis · bff · web
├── .env.example         # compose secrets (JWT, admin seed)
├── pricer/              # C++ engine (CMake, src/ tests/ samples/ schema/ docs/ scripts/)
├── web/
│   ├── shared/          # @thoth/shared: engine client, pool, tag-YAML, grid builder
│   ├── bff/             # NestJS BFF (auth, workspaces, marketdata, schema, grid)
│   ├── frontend/        # Angular SPA (Material + AG Grid + ngx-formly) + nginx
│   └── frontend-react/  # React + Vite POC of the Pricing Grid (same BFF, no backend change)
├── .github/             # CI (runs the engine gates under pricer/)
├── .devcontainer/       # single-container dev (+ opt-in 3-container compose), .vscode/, .claude/
└── todo-gui.md          # web dashboard design notes
```
