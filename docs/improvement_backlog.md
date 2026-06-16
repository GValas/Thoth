# Improvement backlog

Outcome of a full code & architecture review (2026-06-16) across the three layers:
engine/orchestration (`src/tasks`, `src/core`), domain model (`src/contracts`,
`src/underlyings`, `src/marketdata`, `src/vol_correl`, `src/fixings`) and numerics /
MC nodes (`src/nodes`, `src/helpers`). Items are grouped by priority; `file:line`
points at the current code. The headline correctness items were spot-checked
against the source.

> **Status (2026-06-16):** Tier 1 is **done** — yield-curve interpolation, SABR
> forward, correlation PSD validation, PDE grid-Greek denominators, the LSM ITM
> guard, and the Milstein step are all implemented; the theta-roll and
> QuantoAdjustmentNode items were **disproven** by verification (no change needed).
> A full local-vol MCL pipeline (SABR → Dupire → Milstein, for mono/basket/composite)
> was also added on top. **Tiers 2–4 remain open.**

## Tier 1 — Correctness (do first)

* **Flat yield curve.** `Curve::GetCurveValue` (`src/marketdata/curve.cpp:29`) ignores
  the maturity and returns pillar 0 for every date, so every discount factor /
  forward / carry is flat. `_date_list` is stored but never read. Implement
  log-linear (flat-forward) interpolation off `_date_list`/`_value_list`. *Single
  biggest source of mispricing for any multi-tenor book.*
* **SABR uses spot as the forward.** `SabrVolatility::GetImplicitVol`
  (`src/vol_correl/sabr_volatility.cpp:51`, `double F = _spot;`) — Hagan's formula
  is a forward-measure model. Pass the actual forward in; wrong smile (and Dupire
  local vol) in carry-/dividend-rich regimes.
* **Correlation matrix PSD not validated at load.** PSD is only checked inside
  `ComputeCholeskyMatrix` (MCL path); ANA and PDE never call it, so a non-PSD
  YAML matrix silently yields wrong analytic/PDE prices. Add a PSD check in
  `Correlation::SetMatrix`/`SetForexList` (`src/vol_correl/correlation.cpp`) or a
  post-YAML-build validation pass.
* **Per-contract theta roll doesn't propagate `today`.** `ComputeContractGreeks`
  (`src/tasks/pricer.cpp:~244-250`) rolls `_today` and calls `Ctr->SetToday` but not
  `_currency->SetToday` / each underlying's `SetToday`, so a date-dependent
  discount curve reads the base day during the theta reprice. The MCL book-level
  path goes through `InitPricing` and is fine.
* **PDE grid-read delta/gamma formulas are wrong.** `src/tasks/pricer_pde.cpp:~384`
  (delta divides by `GREEK_SPOT_SHIFT` not `X_0*GREEK_SPOT_SHIFT`) and `~389-390`
  (gamma overstated ×4). Currently *dead* — overwritten by bump-and-revalue at
  `pricer.cpp:257` — but a time-bomb. Fix or delete the grid-read path.
* **LSM degenerate-regression guard too low.** `FitAmericanPolicy`
  (`src/tasks/pricer_mcl.cpp:~829`) regresses on `{1,m,m²}` once `itm.size() >= 3`;
  3 points / 3 regressors is interpolation, not regression. Raise to ~50 ITM paths.
* **`_use_milstein` flag is dead.** Parsed and defaulted `true`
  (`src/tasks/mcl_configuration.hpp:16`, `pricer.hpp:20`) but no diffusion node reads
  it — users get Euler-Maruyama. Either implement the Milstein local-vol step in
  `SpotDiffusionNode::ComputeValue` or default it off + warn.

## Tier 2 — Robustness / exception safety

* **Object-graph null-pointer discipline.** Cross-object refs (`_underlying`,
  `_premium_currency`, `_volatility`, `_correlation`, …) are raw non-owning
  pointers dereferenced without guards across `contracts`/`underlyings`/`marketdata`.
  Add one post-YAML-build validation pass asserting the non-null invariants (or
  adopt a `not_null` wrapper) so a missing YAML key fails at build, not deep in the
  pricing tree.
* **LSM regression leaks on throw.** `src/tasks/pricer_mcl.cpp:~836-866` allocates
  raw `la_matrix*`/`la_vector*` and frees manually; use the existing `LaMatrix`/
  `LaVector` RAII wrappers (`src/helpers/raii.hpp`).
* **No MC graph wiring validation.** A node with an unset required pointer reads
  address 0 on the hot path. Add a `Validate()` after `SortNodes`
  (`src/nodes/node_collector.cpp`).
* **Dead / undefined declarations.** `ext_la_matrix_from_symmetric` /
  `ext_la_matrix_to_symmetric` declared (`src/helpers/maths.hpp:77-78`) but never
  defined; `NodeCollector::NewProductNode` unused; `MC_USE_SOBOL`/`MC_USE_MILSTEIN`
  consts (`pricer.hpp:19-20`) unused. Remove or implement.

## Tier 3 — Performance

* **Devirtualize the inner MC loop** (biggest runtime lever). `NodeCollector::PriceNodes`
  (`src/nodes/node_collector.cpp:~188`) does a virtual `ComputeValue`/`UpdateIndicators`
  per (node, step) per path. Order is fixed at setup — a flat function-table or
  `std::variant` dispatch built in `SortNodes` would devirtualize/inline.
* **LSM path-matrix layout.** Stored `[paths × dates]` but the regression reads a
  column per exercise date (strided). Transpose to `[dates × paths]` and
  pre-allocate the `X`/`y` buffers outside the backward-induction loop.
* **Cache `std::poisson_distribution` in `JumpNode`** (`src/nodes/jump_node.cpp:37`) —
  currently constructed every path × step; precompute one per step at `SetDateList`.
* **Cache the Cholesky factor in `Correlation`** — rebuilt + decomposed on every
  bump-and-revalue pass (`src/vol_correl/correlation.cpp:~288`); key by the
  single-name sub-set signature.
* **PDE inner loop:** precompute the `T_1` diagonals outside the time loop (they're
  time-independent, `src/tasks/pricer_pde.cpp:~335`) and reuse the 6 `SolveGrid`
  scratch vectors across the ~7-9 Greek repricings instead of re-allocating.
* **`AbsoluteBasket::GetImplicitVol`** allocates vectors + an n×n matrix per call
  (`src/underlyings/absolute_basket.cpp:~36`); cache scratch / the corr sub-matrix.
* **SABR local vol** calls `GetImplicitVol` 5× per query
  (`src/vol_correl/volatility.cpp:~36`); override `SabrVolatility::GetLocalVolatility`
  with the closed-form Dupire-from-SABR expression.

## Tier 4 — Design / readability

* **Capability dispatch by string.** `Contract::PDE_HasSolution`/`ANA_HasSolution`/
  GPU checks hardcode `GetKind() == KIND_*` chains in every instrument. Replace with
  `virtual bool SupportsAnalytic()/SupportsPDE()` on `Underlying`.
* **Encapsulation breaks.** `Barrier` has public data members
  (`src/contracts/barrier.hpp:8-16`); `Volatility::_is_local` is a public field
  (`src/vol_correl/volatility.hpp:26`) — make private + `virtual bool IsLocal()`.
* **Naming / shadowing.** `Contract::SetGamma(double Delta)` param misnamed;
  single-char `PricerPDE` class members (`s,v,r,k,h,J,N`); loop var `k` shadows the
  `dt` member in `SolveGrid`; `CompositeCorrelNode::GetVolSnode` casing.
* **Stale comments / dead code.** `price_mutex` "global GSL state" comments
  (`src/tasks/../thoth.cpp:120,155`) are stale post-GSL-removal; commented-out method
  blocks in `mono.cpp`/`underlying.cpp`/`equity.cpp`; the disabled quanto block in
  `Equity::GetForward`.
* **`maths.hpp` grab-bag** — add section dividers; consider splitting
  `InterpolateWithSpline` into its own header.
* **Heston ADI grid dims hardcoded** (`pricer_pde.cpp:~542`, `NS/Nv/Nt`) — ignores
  the user's `PdeConfiguration` precision; expose Heston grid sizes.

## Known limitations to document (README → Notes & limitations)

* Discrete dividends unsupported — continuous yield only (`src/marketdata/equity.cpp`).
* `Forex::GetForward` throws — FX forwards / FX options not priced analytically.
* American Greeks use a frozen exercise policy (fit on base paths) — fine for
  first-order, can drift for large bumps; document the regime.
* Variance-swap realized variance is normalised by `T` (continuous-sampling limit)
  rather than the `252/N` ISDA term-sheet convention — confirm intent.

## Reviewed and found OK (no action)

* `QuantoAdjustmentNode::ComputeValue` (`src/nodes/quanto_adjustment_node.cpp:34`) uses
  the cumulative year-fraction `t` (not the step `dt`) in `exp(-ρσσ_fx·t)` applied to
  the diffused spot **level** — this is the correct multiplicative quanto-drift
  adjustment on the level, not a bug.
* `HestonVarianceNode` QE `b²` formula matches Andersen (2008); only a defensive
  `max(0, b2)` before the `sqrt` is worth adding for FP edge-cases near `ψ→2`.
