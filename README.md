# Thoth — monorepo

[![CI](https://github.com/GValas/Thoth/actions/workflows/ci.yml/badge.svg)](https://github.com/GValas/Thoth/actions/workflows/ci.yml)

Thoth is an equity-derivatives pricing engine plus a web dashboard, kept in one
repository:

| Path | What it is |
|------|------------|
| [`pricer/`](pricer/README.md) | **The C++23 pricing engine** — Monte-Carlo, PDE and analytic pricers; multi-asset / multi-currency books; YAML-configured; usable as a batch tool or an HTTP service (`thoth -server`). This is the mature, fully-tested core. See **[`pricer/README.md`](pricer/README.md)**. |
| `web/` | **The web dashboard** *(under construction)* — a NestJS BFF + Angular SPA over the engine: edit market data via schema-driven forms, and compute strike × maturity price grids (heatmap + Greeks). The engine is used unmodified as a pricing microservice. |

## Engine quickstart

```bash
cmake -B pricer/build pricer && cmake --build pricer/build -j
ctest --test-dir pricer/build --output-on-failure
./pricer/build/thoth -batch pricer/samples/simple_call.yaml /tmp/out.yaml   # premium ~ 15.71
```

Full engine documentation — pricers, instruments, market data, the YAML format,
the HTTP server contract — is in [`pricer/README.md`](pricer/README.md).

## Web dashboard

The dashboard talks to the engine over its HTTP API (`POST /price` with YAML,
`GET /health`, `GET /progress`). Architecture, build and run instructions will live
here as `web/` lands; the design is tracked in [`todo-gui.md`](todo-gui.md).

## Repository map

```
Thoth/
├── README.md            # this file (monorepo map)
├── CLAUDE.md            # agent mandate (build/test, README sync)
├── docker-compose.yml   # (coming) prod: pricer ×N · redis · bff · web
├── pricer/              # C++ engine (CMake, src/ tests/ samples/ schema/ docs/ scripts/)
├── web/                 # (coming) NestJS BFF + Angular SPA
├── .github/             # CI (runs the engine gates under pricer/)
├── .devcontainer/  .vscode/  .claude/
└── todo-gui.md          # web dashboard design notes
```
