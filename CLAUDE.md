# CLAUDE.md — IT Quant Agent mandate

Guidance for Claude (and any agent) working in this repository.

## Monorepo layout

This is a monorepo. The C++ pricing engine lives under **`pricer/`** (its own
`README.md`, `CMakeLists.txt`, `src/ tests/ samples/ schema/ docs/ scripts/`); the
web dashboard lives under **`web/`** (NestJS BFF + Angular SPA, added incrementally),
which also hosts **`web/mcp/`**, the MCP server exposing the engine to LLM agents
(stdio + Streamable HTTP; see the root `README.md` for transports, auth and the
claude.ai connector setup).
The root holds the monorepo `README.md`, this file, `docker-compose.yml`, and the
shared `.github/ .devcontainer/ .vscode/`.

## MANDATORY: keep the README in sync before every commit/push

**Before any `git commit` or `git push`, update the relevant `README.md` so it
reflects the change being committed.** This is non-negotiable: the README is the
single place where the project's evolutions are followed, so it must never lag the
code.

- An engine change → **`pricer/README.md`** (the detailed engine doc).
- A web / monorepo-structure change → the **root `README.md`** (repo map + web).

Concretely, before committing/pushing, check whether the change touches anything
`pricer/README.md` documents and update the matching section:

- a new/changed/removed **pricer, instrument, underlying, market-data or
  volatility kind** → *Features*
- a new/renamed/removed **run wrapper or CLI mode** → *Usage* and *Repository layout*
- a new/changed **sample** in `samples/` → the `samples/` paragraph in *Configuration*
- a new **YAML field / configuration switch** → *Configuration* (and the example if relevant)
- a new **build/test/tooling** step or dependency → *Build*
- a known **limitation** added or lifted → *Notes & limitations*

If a change genuinely needs no README edit (pure internal refactor, formatting,
a bug fix with no user-visible or documented behaviour change), that is allowed —
but say so explicitly in the commit so the decision is visible. When in doubt,
update the README.

Only commit or push when the user asks (the usual rule). When you do, the README
sync above is part of that same commit.

## Build & test (engine)

All engine commands run against the `pricer/` source tree:

```bash
cmake -B pricer/build pricer && cmake --build pricer/build -j   # -> pricer/build/thoth (+ thoth_tests)
ctest --test-dir pricer/build --output-on-failure              # doctest suite
pricer/scripts/format.sh            # clang-format over pricer/src and pricer/tests (--check is the CI gate)
```

## Layout (engine)

Under `pricer/`: `src/` engine + instruments + market data; `tests/` doctest suite;
`samples/` runnable YAML books; `schema/thoth.schema.json` the config JSON Schema;
`docs/` design notes; `scripts/` shell wrappers (run from `pricer/`, e.g.
`pricer/scripts/format.sh`). See `pricer/README.md` for the engine, and the root
`README.md` for the monorepo map and the web dashboard.
