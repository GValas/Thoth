# Volatility models

How Thoth represents a volatility and feeds it to the three pricing engines
(ANA / PDE / MCL). Every volatility is a `Volatility` subclass (`src/vol_correl/`)
referenced by an `!equity` through its `volatility:` field, so swapping the model
is a one-line book change. A volatility exposes two things to the rest of the
engine:

- `GetImplicitVol(Strike, Forward, MaturityDate)` — the Black implied vol the ANA
  leg and the PDE grid bounds read. `Strike = 0` is a sentinel for the reference
  (ATM) level. `Forward` is the owning underlying's forward to the maturity; only
  a forward-measure surface (SABR) uses it, the flat surfaces ignore it.
- `GetNode(NodeCollector&)` — the Monte-Carlo diffusion node.

Two flags drive the dispatch: `_is_local` (the MCL engine must build a Dupire
local-vol grid rather than a single constant) and `IsStochastic()` (a genuine
variance factor — Heston/Bates).

A shared calendar weight applies to every flat surface. `Volatility::GetDayWeight`
returns `sqrt((5 + 2 w) / 7)` where `w = non_working_days_weight`, scaling the
quoted vol so that weekends carry weight `w` (w = 1 ⇒ no-op). It is configured by
the optional `calendar:` field (a `!simple_weighted_calendar`).

---

## 1. Constant — `bs_volatility`

A single flat lognormal vol. `_is_local = false`, `IsStochastic() = false`.

- ANA / PDE read `GetImplicitVol = (vol + shift) * GetDayWeight()` — strike-,
  forward- and maturity-independent (`src/vol_correl/bs_volatility.cpp`).
- MCL builds one `ConstantNode` carrying the same `(vol + shift) * GetDayWeight()`,
  so the three engines diffuse the identical number. The diffusion is the
  **exact log-Euler step** `d(lnS) = (r − v²/2) dt + v dW` in
  `SpotDiffusionNode::ComputeValue` — exact for a constant `v`, so there is no
  discretisation bias.
- `shift` (`_vol_shift`) is the additive parallel bump the bump-and-revalue vega
  applies; zero in normal pricing.

```yaml
flat_vol: !bs_volatility
  volatility: 20            # percent
  calendar: calendar        # optional
calendar: !simple_weighted_calendar
  non_working_days_weight: 1
```

---

## 2. SABR — `sabr_volatility`

A SABR **implied-volatility surface** via the Hagan (2002) lognormal
approximation (`src/vol_correl/sabr_volatility.cpp`). Per-maturity parameters
`alpha` (level), `beta` (CEV exponent, 1 = lognormal), `rho` (spot/vol
correlation, the skew), `nu` (vol-of-vol) are **linearly interpolated in time**
(`Interp`, flat beyond the first/last maturity).

The formula is the forward-measure Hagan series, so it is **evaluated at the
forward** `F` supplied by the underlying (`Single::GetImplicitVol` passes
`GetForward(maturity)`). With `Strike = 0` it falls back to `K = F` (the ATM
level). `_is_local = true`: SABR has no Monte-Carlo node of its own — `GetNode`
errors; the MCL engine instead derives a Dupire local-vol grid from this surface
(§3–4).

```yaml
sabr_vol: !sabr_volatility
  maturities: [0.25, 0.50, 1.00, 2.00]   # years, strictly ascending
  alpha:      [0.22, 0.21, 0.20, 0.19]   # ATM level (decimals)
  beta:       [1.00, 1.00, 1.00, 1.00]   # CEV exponent in [0,1]
  rho:        [-0.60, -0.55, -0.50, -0.45] # spot/vol correl, the skew
  nu:         [0.70, 0.55, 0.40, 0.30]   # vol-of-vol (decimals)
  calendar: calendar
```

---

## 3. Implied vs local vs Dupire

A quoted smile is an **implied** surface `σ_imp(K, T)`: each (strike, maturity)
carries the flat Black vol that reprices that option. You cannot diffuse with it
directly — a Monte-Carlo path needs the **instantaneous local** vol
`σ_loc(S, t)`, the vol the spot actually feels when it sits at level `S` at time
`t`. Dupire's formula converts one to the other:

`Volatility::GetLocalVolatility(Strike, MaturityDate, Spot, r, q)`
(`src/vol_correl/volatility.cpp`) evaluates the Dupire local vol by finite
differences of the implied surface: a first/second strike derivative
(`dK = 0.01`) and a ±1-calendar-day maturity derivative (`dT = 1/365`), with the
forward `F = S·exp((r − q)·T)` recomputed at each bumped maturity and `d1` from
the mid vol. The result `sqrt(N/D)` is the standard Dupire ratio.

`Equity::GetLocalVolatility(Strike, MaturityDate)` supplies the market context:
`Spot = _spot`, `r` from the currency's yield curve, and `q` = repo +
continuous-dividend yield (each independently optional, summed). This is the only
call the local-vol node builder makes per grid point.

The three engines consume the surface differently:

| engine | what it reads |
| --- | --- |
| ANA (vanilla) | `GetImplicitVol(strike, T)` — implied vol **at the option's strike** (`src/contracts/vanilla.cpp`); reprices the smile exactly |
| PDE | `GetImplicitVol(0, T)` — **ATM only**, a single flat vol per grid (`src/tasks/pricer_pde.cpp`); does not reprice the smile (see [pde.md](pde.md)) |
| MCL | the **Dupire local-vol grid** (§4) for a local surface; a constant node for `bs_volatility` |

---

## 4. Local-vol diffusion in MCL

For a local surface (`_is_local`), `Single::GetNode` wires a
`LocalVolatilityNode` into the `SpotDiffusionNode`
(`Single::BuildLocalVolNode`, `src/marketdata/single.cpp`).

**Grid construction.** For each diffusion date `t_k` the builder lays a log-spot
grid centred on the spot: half-width `8 · σ_atm · sqrt(T)` (factor
`LOCAL_VOL_GRID_SIGMA_FACTOR = 8`, generous so almost every path lands inside it),
`LOCAL_VOL_GRID_POINTS = 201` points (odd so a node sits on the spot). `σ_atm` is
the ATM implied vol used **only** for sizing. Each grid point `i` sits at
log-spot `(offset + i) · ln_step` and stores `Equity::GetLocalVolatility(s_grid,
t_k)` — i.e. the full Dupire surface sampled onto that date's grid.

**Path read-out.** `LocalVolatilityNode::ComputeValue` reads the local vol at the
spot reached on the **previous** step by clamped linear interpolation along that
date's grid (a path past the boundary reuses the edge local vol rather than
reading out of range). `LogSpotDerivative` returns the central-difference
`dσ/d(lnS)` on the same grid — the state-derivative the Milstein step needs (§5).
The scheduler dependency (`GetDateDependencies`) makes the local vol at date `k`
wait for the spot at date `k − 1`.

**Underlying coverage.**
- **Mono** delegates straight to `Single::GetNode`, so the component gets its own
  Dupire grid.
- **Basket / absolute-basket** build a `Single` spot diffusion per component, so
  each leg carries its own local-vol grid (correlation is layered on the noise).
- **Composite (quanto)** diffuses the *inner* underlying on its local-vol grid;
  the **quanto drift correction** uses an ATM scalar vol, not the local surface
  (`Composite::GetNode` / `GetVolNode` → `Single::GetVolNode`, which returns a
  `#atmvol` constant node at the last diffusion date — the same ATM vol the
  ANA/PDE quanto correction uses).

**Dupire repricing property.** A local-vol MC built from an implied surface
reproduces the implied-vol prices it was derived from. `samples/sabr_call.yaml`
demonstrates this: the same book is priced by ANA (Black at the SABR implied vol
of each strike) and MCL (Dupire local vol from that SABR surface) and the two
**agree within Monte-Carlo error**. The PDE leg is deliberately not run there
because it would use a single ATM vol and miss the smile.

---

## 5. Milstein correction

The constant-vol step is log-Euler. When the vol is state-dependent (local vol),
`SpotDiffusionNode::ComputeValue` adds the **log-space Milstein correction**

`+ ½ · v · (dv/dlnS) · (dW² − dt)`

where `dv/dlnS` comes from `LocalVolatilityNode::LogSpotDerivative`. This lifts
the scheme from strong order 0.5 (Euler) to **strong order 1.0**.

It is **always on** — there is no config flag. The correction term vanishes
identically when `v` is constant (`dv/dlnS = 0`), so it is a no-op for
`bs_volatility` and only ever refines the genuine local-vol diffusion. It is
enabled via `SpotDiffusionNode::EnableMilstein`, called only on the local-vol
branch of `Single::GetNode`.

---

## 6. Heston — `heston_volatility` (stochastic vol)

A genuine two-factor stochastic-volatility model (`src/vol_correl/heston_volatility.*`).
`IsStochastic() = true`:

```
dS = (r − q) S dt + sqrt(v) S dW^S
dv = kappa (theta − v) dt + xi sqrt(v) dW^v,   d⟨W^S, W^v⟩ = rho dt
```

**YAML fields** (vols quoted in percent, converted to variances by the registry):

```yaml
heston_vol: !heston_volatility
  spot: 100
  init_vol: 25       # sqrt(v0)    in percent  -> v0     = (0.25)^2
  long_vol: 25       # sqrt(theta) in percent  -> theta  = (0.25)^2
  kappa: 2           # mean-reversion speed
  vol_of_vol: 0.5    # xi
  calendar: calendar
```

`rho` is **not** a field on the vol object: the spot/variance correlation lives
in the global `!correlation_matrix`, between the equity `eq` and its variance
pseudo-underlying `eq_var` (see `samples/heston_call.yaml`, `matrix: [1, -0.7,
-0.7, 1]`). The vega bump shifts both `v0` and `theta` by `(sqrt(v) + shift)²` so
bump-and-revalue vega works for Heston too (`HestonVolatility::Shifted`).

Engine support (cross-check `samples/heston_call.yaml`, all three agree):
- **MCL** — `Single::GetNode` builds a `HestonVarianceNode` (CIR variance) and a
  `HestonSpotNode` driven by it, discretised with the **Andersen QE**
  (quadratic-exponential) scheme — far less biased on `sqrt(v)` than Euler. The
  variance factor uses the shared `#vol_white_noise`, the spot `#white_noise`;
  `rho` enters through the correlation matrix.
- **PDE** — a **2-D `(S, v)` Douglas-ADI** grid with the cross `∂²/∂S∂v` term.
- **ANA** — the **characteristic-function** vanilla pricer (Carr–Madan /
  Little-Heston-Trap branch-stable form), also the calibration engine.

`GetImplicitVol` returns only a coarse `sqrt(v0)` proxy for incidental callers
(PDE grid bounds, quanto fallback); the engines that understand Heston read the
parameters directly. See `samples/heston_call.yaml` for a complete book.

---

## 7. Bates — Heston + jumps

Bates extends Heston with **lognormal jumps** (a compound Poisson process on the
spot). It is the same `!heston_volatility` object with three extra optional
fields (absent ⇒ 0 ⇒ pure Heston):

```yaml
heston_vol_bates: !heston_volatility
  spot: 100
  init_vol: 25
  long_vol: 25
  kappa: 2
  vol_of_vol: 0.5
  jump_intensity: 0.5   # lambda, jumps per year
  jump_mean: -0.10      # log-jump mean (crash-like when negative)
  jump_vol: 0.15        # log-jump vol
```

`HasJumps()` is true once `jump_intensity > 0`.

- **MCL** — a compound-Poisson jump (`#jump_noise`) layered on the QE spot
  (`Single::GetNode` wires `SetJumpNode` when `HasJumps()`).
- **ANA** — a closed-form lognormal jump factor multiplies the Heston
  characteristic function.
- **PDE** — **not supported** (it would need a PIDE); the `samples/heston_call.yaml`
  book runs Bates only through ANA and MCL, which agree, and the jumps lift the
  premium above pure Heston.

---

## 8. Possible extensions

Honest, not-yet-implemented directions:

- **Local-stochastic vol (LSV)** — a calibrated leverage surface `L(S, t)` on top
  of Heston, multiplying the local instantaneous vol so the model reprices the
  full implied surface *and* keeps Heston's forward-smile dynamics. The most
  accurate option, but needs a particle/PDE leverage calibration on top of a
  calibrated Heston — a sizeable addition.
- **SABR Monte-Carlo** — diffuse the SABR SDE directly (stochastic `alpha`)
  rather than only its Dupire local-vol projection, to capture forward-smile
  dynamics SABR implies but the static surface loses.
- **Multi-asset local vol** — the per-component local-vol grids (§4) already
  diffuse a correlated basket under local vol; a joint local-correlation /
  local-vol calibration across assets would tighten basket-smile consistency.
- **SABR / Heston term-structure of correlation and vol-of-vol** — Heston is
  currently single-bucket (scalar `kappa/theta/xi/rho`); a maturity term
  structure (as SABR already has for `alpha/beta/rho/nu`) would improve the fit
  across expiries.

---

## See also

- [running.md](running.md) — running a book through the engines
- [monte_carlo.md](monte_carlo.md) — the MCL node graph and path engine
- [pde.md](pde.md) — the finite-difference grids (1-D CN, 2-D ADI)
- [products.md](products.md) — instruments and payoffs
- [gpu.md](gpu.md) — GPU Monte-Carlo
- [agent.md](agent.md) — agent mandate and conventions
