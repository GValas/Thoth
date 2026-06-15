# Thoth — global code review (2026-06-15)

Findings from a multi-dimensional review (quant correctness, C++ quality, style,
latent bugs), each verified against the actual code and the cross-engine matrix
results. Ordered by priority within each section.

## A. Confirmed bugs / defects

- [ ] **`vol_time_step` is `int` but configs pass `0.01`** — `GetInteger` truncates
      it to `0` silently (`mcl_configuration.hpp:13`, `object_registry.cpp:431`).
      Today the field is unused so there's no live impact, but the type is wrong.
      Fix: make it `double` + `GetDouble`. Also wire it in or remove it (and
      `min_time_step`, which has zero readers — a dead config knob).
- [ ] **No lower-bound check on `paths` / `max_time_step`** (`object_registry.cpp:428-431`).
      `max_time_step: 0` → `days_step(0)` never advances → infinite hang
      (`pricer_mcl.cpp:27`); `paths: 0` → divide-by-N → NaN premium reported as a
      plausible result. Fix: validate `> 0` at load, `ERR` otherwise.
- [ ] **MCL diffusion vol omits the day-weight** that ANA/PDE apply.
      `BsVolatility::GetImplicitVol` returns `(vol+shift)*GetDayWeight()` but
      `BsVolatility::GetNode` (the MCL constant) uses `vol+shift` without it
      (`bs_volatility.cpp:22` vs `:28`). Engines disagree whenever
      `non_working_days_weight != 1` (default 1 masks it). Fix: apply
      `GetDayWeight()` in `GetNode` too.
- [ ] **GSL leaks on the `ERR` (throw) path** — raw `gsl_*` allocs not freed when an
      exception unwinds: `AbsoluteBasket::GetImplicitVol` (fwds/vols/correls,
      `absolute_basket.cpp:36-77`), `HistoricalCorrelationComputation::Execute`,
      PDE `SolveGrid` (six `gsl_vector*`), `ext_gsl_matrix_to_near_positive`.
      Use the existing `GslVector`/`GslMatrix` RAII (`gsl_raii.hpp`). (Leak only on
      the error path, which usually aborts, so low impact — but trivially fixable.)
- [ ] **`LocalVolatilityNode::_vol_vector_list`** (`vector<gsl_vector*>`) never frees
      its elements (`local_volatility_node.hpp:12`). Dead today; leaks once local-vol
      MCL is wired. Use `vector<GslVector>`.

## B. Robustness / correctness to verify

- [ ] **Periodic spline on a non-periodic grid** — `InterpolateWithSpline` uses
      `gsl_interp_cspline_periodic` (`maths.cpp:348`) for the PDE price readout.
      Prices currently match ANA to 4 dp, so it's accurate in practice, but periodic
      is semantically wrong (assumes `y[0]==y[n-1]`). Switch to `gsl_interp_cspline`.
- [ ] **Composite quanto forward extra `v_fx^2` term** (`composite.cpp:78`,
      `qto = exp((rho*v_fx*v_eq + v_fx*v_fx)*dt)`) — only on the third-currency
      (genuinely-quanto) composite path, which the matrix does not exercise. Verify
      against the MCL node graph for a composite settled in a third currency.
- [ ] **Cluster pseudo-random streams not disjoint** — slaves differ only by RNG
      seed (`thoth.cpp:265`, `pricer_mcl.cpp:69`). Sobol is correctly skipped per
      slave, but the pseudo-random tail (non-Sobol mode, Heston variance/jump noise)
      can overlap → biased combined `premium_trust`/premium for `-cluster` runs with
      `use_sobol:false` or any Heston/Bates book. Use independent substreams
      (leapfrog / hashed seed).
- [ ] **Heston/Bates integration return code ignored** — `gsl_integration_qagiu`
      result discarded (`finance.cpp`); extreme `xi`/maturity/strike can silently
      return a degraded value. Check the return code and log a warning.
- [ ] **SLN moment-matching numerics** — exact float compares `E3 == 0` /
      `E2 <= 0` (`maths.cpp`) should use a relative tolerance; the `ERR("??????")`
      message (`maths.cpp:133`) should state the invariant.

## C. Performance (hot paths)

- [ ] **`FitAmericanPolicy`** allocates GSL regression buffers (X, y, beta, cov,
      workspace) per exercise date (`pricer_mcl.cpp`). Hoist to max size outside the
      loop and reuse via views.
- [ ] **Heston `heston_probability`** allocates a 1000-interval workspace per P1/P2
      per call (thousands of times under bump-and-revalue). Cache/reuse it.
- [ ] **Basket variance-swap ANA** re-runs the full SLN moment-match (`LN_to_M4` is
      O(n^3) in members) for each of ~801 strikes (`absolute_basket.cpp`). Cache the
      moment-matching across the strike loop.

## D. Style / consistency

- [ ] Add a one-line `//!` class-purpose comment to the ~30 headers missing one
      (most of `marketdata/`, `nodes/`, `underlyings/`, `contracts/`).
- [ ] Fix `Contract` member naming (`contract.hpp:34-37`): apply the `_member`
      convention (currently `vect_idx_flow_date`, `idx_underlying`), fix the
      "underling" typo. Document the PDE math-struct bare-name exception.
- [ ] Casing bug: `GetVolSnode` → `GetVolSNode` (`composite_correl_node.hpp:23`)
      to match its siblings/setters.
- [ ] Const-correctness: make non-mutating getters `const` (only ~8 files do today);
      `Contract::GetPremium()` vs `Book::GetPremium() const`.
- [ ] Encapsulation: `Barrier` exposes its state as public members
      (`barrier.hpp:6-15`) unlike the other contracts — make private + accessors or
      document as a data holder.
- [ ] Vocabulary: `SetCompoCurrency` → `SetCompositeCurrency`; `Udl*` → `Underlying*`
      in public node APIs (`quanto_adjustment_node`).
- [ ] Quick wins: stray `//` → `//!` (contract.hpp, constants.hpp, finance.hpp,
      underlying.hpp); delete dead commented code (`underlying.hpp:15-32`); normalise
      constructor-comment boilerplate (`book.hpp:56` `// !`/reversed,
      `yield_curve.hpp:11` "contructor" typo).

## E. Investigated but NOT bugs (false positives — recorded for transparency)

- **PDE gamma is NOT 4x wrong, and PDE delta is correct.** A reviewer flagged the
  `GridResult` delta/gamma denominators; the matrix shows PDE delta/gamma match
  ANA/MCL to ~0.3% across every product. The `GridResult` delta divides by
  `GREEK_SPOT_SHIFT` whereas `barrier.cpp` divides by `s*GREEK_SPOT_SHIFT` — an
  apparent unit inconsistency, but the results are correct (the grid/`Psi`
  normalisation accounts for it). Worth only a clarifying comment, not a fix.
- **`QuantoAdjustmentNode` using cumulative time `t_i` (not `dt`) is correct** —
  the spot level at each date must reflect the cumulative quanto drift from today.
- **Node index-0 handling and `Correlation::ComputeCholeskyMatrix` clearing** are
  correct (these were the real bugs already fixed earlier in this session, along
  with composite-spot recording scheduling and basket `GetImplicitVol(0)`).
