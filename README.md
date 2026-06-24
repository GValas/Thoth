# Thoth — monorepo

[![CI](https://github.com/GValas/Thoth/actions/workflows/ci.yml/badge.svg)](https://github.com/GValas/Thoth/actions/workflows/ci.yml)

Thoth is an equity-derivatives pricing engine plus a web dashboard, kept in one
repository:

| Path | What it is |
|------|------------|
| [`pricer/`](pricer/README.md) | **The C++23 pricing engine** — Monte-Carlo, PDE and analytic pricers; multi-asset / multi-currency books; YAML-configured; usable as a batch tool or an HTTP service (`thoth -server`). This is the mature, fully-tested core. See **[`pricer/README.md`](pricer/README.md)**. |
| `web/` | **The web dashboard** — a NestJS BFF + Angular SPA over the engine: edit market data via schema-driven forms, and compute strike × maturity price grids (heatmap + Greeks). The engine is used unmodified as a pricing microservice. |

## Engine quickstart

```bash
cmake -B pricer/build pricer && cmake --build pricer/build -j
ctest --test-dir pricer/build --output-on-failure
./pricer/build/thoth -batch pricer/samples/simple_call.yaml /tmp/out.yaml   # premium ~ 15.71
```

Full engine documentation — pricers, instruments, market data, the YAML format,
the HTTP server contract — is in [`pricer/README.md`](pricer/README.md).

## Web dashboard

Three tiers — Angular SPA → NestJS BFF → N stateless `thoth -server` replicas:

```
Browser ─/api─▶ nginx+SPA ─▶ NestJS BFF ─┬─ BullMQ/Redis queue ─▶ EnginePool leases 1 of N
 (Material/AG Grid)                       └─ SQLite (TypeORM)        thoth -server replica
```

The BFF talks to the engine over its HTTP API (`POST /price` with YAML, `GET /health`,
`GET /progress`); a pricing job leases one replica for its duration, which makes that
replica's progress the job's progress. Two tabs: **Market Data** (schema-driven forms
from the engine's JSON Schema) and **Pricing Grid** (strike × maturity heatmap + Greeks;
engine is user-selected — note CPU Monte-Carlo gives book-level Greeks only). Auth is
JWT + rotating refresh cookie with an admin RBAC tab.

**Run (prod, Docker):**

```bash
cp .env.example .env        # then edit the secrets
docker compose up --build   # pricer1/2 · redis · bff · web
# open http://localhost:8088   (login with ADMIN_EMAIL / ADMIN_PASSWORD)
```

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
├── docker-compose.yml   # prod: pricer1/2 · redis · bff · web
├── .env.example         # compose secrets (JWT, admin seed)
├── pricer/              # C++ engine (CMake, src/ tests/ samples/ schema/ docs/ scripts/)
├── web/
│   ├── shared/          # @thoth/shared: engine client, pool, tag-YAML, grid builder
│   ├── bff/             # NestJS BFF (auth, workspaces, marketdata, schema, grid)
│   └── frontend/        # Angular SPA (Material + AG Grid + ngx-formly) + nginx
├── .github/             # CI (runs the engine gates under pricer/)
├── .devcontainer/       # single-container dev (+ opt-in 3-container compose), .vscode/, .claude/
└── todo-gui.md          # web dashboard design notes
```
