# Stochastic volatility — proposal

Thoth currently diffuses each underlying as GBM with a deterministic vol (flat
`bs_volatility`, or a `sabr_volatility` surface used only at its ATM level). This
note proposes adding a genuine stochastic-volatility (SV) model and how to wire
it into the three engines (MCL / PDE / ANA).

## Recommended model: Heston

```
dS_t = (r - q) S_t dt + sqrt(v_t) S_t dW^S_t
dv_t = kappa (theta - v_t) dt + xi sqrt(v_t) dW^v_t
d⟨W^S, W^v⟩_t = rho dt
```

Parameters per (underlying, maturity bucket): `v0` (initial variance), `kappa`
(mean-reversion speed), `theta` (long variance), `xi` (vol-of-vol), `rho`
(spot/vol correlation). Feller `2 kappa theta >= xi^2` keeps `v_t > 0`.

Why Heston rather than SABR-dynamics or a local-stoch-vol (LSV) hybrid:
- **Semi-analytic European price** via the characteristic function — gives a fast
  ANA leg *and* a calibration target (the existing `sabr_volatility` has no such
  thing; it is a static implied surface).
- Captures **skew and smile dynamics** (mean-reverting variance), the usual
  reason to leave flat BS.
- Well-understood, robust discretisation schemes for MC (QE) and PDE (ADI).
- Mean-reversion + `rho` map cleanly onto the existing correlation machinery.

(SABR is better for a single-expiry rates smile; LSV is the most accurate but
needs a calibrated leverage surface on top of Heston — propose it as a phase 2.)

## How it plugs into Thoth

New market-data object `!heston_volatility` (a `Volatility` subclass) carrying
`v0/kappa/theta/xi/rho` (with `spot`, like `sabr_volatility`). It is referenced
from an `!equity` exactly like today's vols, so books need no structural change.

### MCL (Monte-Carlo) — node graph

The node graph already has `NoiseNode → BrownianNode → SpotDiffusionNode` per
underlying. Add, per Heston underlying:
- a **variance node** `v_t` integrating the CIR process with the **Andersen QE**
  scheme (quadratic-exponential; far less biased than Euler on `sqrt(v)`),
- a second correlated Brownian for `W^v` (reuse `CorrelatedNoiseNode` /
  Cholesky — `rho(S,v)` slots into the same correlation matrix), and
- a Heston spot node that reads `v_t` instead of a constant vol node.

This is additive: `bs_volatility` underlyings keep the current `SpotDiffusionNode`.
The single-tree Greeks and Sobol/bridge layout carry over unchanged (the
variance Brownian just adds dimensions). Effort: moderate (~3 new node types +
the QE step + the equity/registry wiring).

### ANA (analytic) — characteristic function

European vanilla by the Heston characteristic function and a single numerical
integral (Gil-Pelaez / Carr–Madan; the "Little Heston Trap" branch-stable form).
This is a closed-ish form: `ANA_EvalPrice` for a Heston-vol vanilla evaluates one
integral. It also becomes the **calibration** engine. Effort: moderate, self-
contained (one new pricing routine + a robust complex-integrand quadrature).

### PDE — 2D grid (the big one)

Heston is a 2-factor PDE in `(S, v)`, so the current **1-D Crank-Nicolson grid
does not extend directly**. Options, cheapest first:
1. **MCL/ANA only** at first (skip PDE) — covers American via MC LSM and European
   via the char-function; ship SV without the 2-D solver.
2. **2-D ADI** (Craig–Sneyd / Hundsdorfer–Verwer) on an `(S, v)` grid with the
   cross `∂²/∂S∂v` term — supports American by the usual projection. This is a
   substantial new solver (a few hundred lines + boundary conditions), so propose
   it as its own phase.

## Suggested phasing (each phase = build + tests + commit)

1. `!heston_volatility` object + registry + an MCL QE diffusion node; validate MC
   European prices against a reference.
2. ANA Heston char-function pricer; cross-check ANA vs MCL (and use it to
   calibrate). Wire Greeks (bump works as-is for MCL/ANA).
3. PDE 2-D ADI solver for Heston (European + American), cross-checked vs ANA/MCL.
4. (optional) LSV: a local leverage surface on top of calibrated Heston.

## Validation plan

- MCL European vs ANA char-function (within MC error) across strikes/maturities.
- Put–call parity per engine.
- Feller-violating params: variance stays non-negative (QE / ADI floor).
- Smile shape: `rho < 0` produces a downward skew; `xi` fattens the wings.
- Degenerate `xi → 0, kappa large` collapses to BS at `sqrt(theta)` — assert it
  matches the existing `bs_volatility` engine.
