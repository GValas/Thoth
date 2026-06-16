# CLAUDE.md — IT Quant Agent mandate

Guidance for Claude (and any agent) working in this repository.

## MANDATORY: keep README.md in sync before every commit/push

**Before any `git commit` or `git push`, update `README.md` so it reflects the
change being committed.** This is non-negotiable: the README is the single place
where the project's evolutions are followed, so it must never lag the code.

Concretely, before committing/pushing, check whether the change touches anything
the README documents and update the matching section:

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

## Build & test

```bash
cmake -B build && cmake --build build -j      # -> ./build/thoth (+ thoth_tests)
ctest --test-dir build --output-on-failure    # doctest suite
./format.sh            # clang-format over src/ and tests/ (--check is the CI gate)
```

## Layout

`src/` engine + instruments + market data; `tests/` doctest suite; `samples/`
runnable YAML books; `docs/` design notes. See `README.md` for the full picture.
