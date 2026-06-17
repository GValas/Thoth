# PDE finite-difference engine

The `pde` pricing method (`PricerPDE`, `src/tasks/pricer_pde.cpp`) is Thoth's
grid-based engine. It backwards-solves the Black-Scholes PDE on a single
underlying with a Crank-Nicolson scheme on a non-uniform grid, and falls back to
a 2-D Douglas-ADI solve for Heston (stochastic-vol) vanillas. It prices European
and American payoffs, continuous and discrete knock-out / knock-in barriers, and
handles quanto carry corrections.

It is selected per pricer through `!pricer_configuration` (`method: pde`) plus a
`!pde_configuration` object. See [running.md](running.md) for the run wrappers.

## 1. The 1-D Black-Scholes solver

### The equation and the coordinate change

The pricer works on the backward PDE (header block comment in
`pricer_pde.hpp`):

```
V_t = A(X) V_X + B(X) V_XX + C(X) V
```

with coefficients (`A`, `B`, `C` in the `.cpp`):

```
A(X) = -r X            (note: this is the V_X coefficient, see below)
B(X) = -0.5 v^2 X^2
C(X) = r_disc
```

Read the code carefully: the member `A(x)` returns `-0.5 v^2 x^2` (the diffusion
term) and `B(x)` returns `-r x` (the drift). The header's symbol naming and the
implementation swap roles versus the comment, but the assembled operator is the
standard BS generator: `v` is the vol, `r` the carry, `r_disc` the discount.

To concentrate grid nodes near spot, the spatial variable `X` is mapped through
a **sinh transform** `X = Phi(x)`:

```
Phi(x)    = X_0 + cc * sinh(aa*x + bb)
Phi_x(x)  = cc*aa * cosh(aa*x + bb)
Phi_xx(x) = cc*aa^2 * sinh(aa*x + bb)
Psi(X)    = (asinh((X - X_0)/cc) - bb) / aa      // inverse, x = Psi(X)
```

`X_0` is set to spot, so `Phi` packs resolution around the money. The constants
are fixed so the computational interval `x in [x_min, x_max]` maps exactly onto
`[X_min, X_max]`: `cc = 0.02`, `c1 = asinh((X_min - X_0)/cc)`,
`c2 = asinh((X_max - X_0)/cc)`, `aa = c2 - c1`, `bb = c1`.

Under the change of variable `V(X,t) = u(x,t)`, the PDE becomes
`u_t = a(x) u_x + b(x) u_xx + c(x) u` with the transformed coefficients:

```
a(x) = A(Phi(x)) / Phi_x(x)^2
b(x) = B(Phi(x)) / Phi_x(x) - A(Phi(x)) * Phi_xx(x) / Phi_x(x)^3
c(x) = C(Phi(x))
```

### The spatial operator L and the Crank-Nicolson systems

The transformed operator `L` is discretised on a uniform `x`-grid of step `h`
(central differences), giving the tridiagonal diagonals:

```
L_u(j) =  b(jh)/(2h) + a(jh)/h^2     // super-diagonal (couples u(j+1))
L_m(j) =  c(jh)      - 2 a(jh)/h^2   // main diagonal
L_d(j) = -b(jh)/(2h) + a(jh)/h^2     // sub-diagonal (couples u(j-1))
```

so that `u_t = L u = L_d(j) u(j-1) + L_m(j) u(j) + L_u(j) u(j+1)`.

Time stepping is the **theta scheme** with `theta = PDE_THETA = 0.5`
(Crank-Nicolson), with timestep `k = T_max / N`. Each step solves

```
U_0 . T_0 = U_1 . T_1
T_0 = I + k (1 - theta) L      (assembled into the LHS diagonals, n step)
T_1 = I - k theta L            (applied to the known layer, n+1 step)
```

`SolveGrid` builds the three diagonals `diag_u / diag_m / diag_d` from `T_0`
once (with Dirichlet rows pinned: `diag_m(0)=diag_m(J)=1`, off-diagonals zeroed),
then for each timestep `i = N-1 .. 0` forms the right-hand side `D_1` from `T_1`
applied to the previous layer (boundary entries set to `u_dw` / `u_up`) and calls
`SolveTridiagonal(diag_m, diag_u, diag_d, D_1, U_0)`. The price at spot is read
by spline-interpolating the final layer at `x_0 = Psi(X_0)` (`GetGridPrice`).

## 2. Grid sizing and the single-vol approximation

The original-grid extents are set in `InitGrid`:

```
X_0   = spot
X_max = spot * exp(+ sigma_factor * v * sqrt(T))
X_min = spot * exp(- sigma_factor * v * sqrt(T))
```

with `T = YearFraction(_today, maturity)`. `sigma_factor` is
`pde_configuration.custom_sigma_factor` (default `PDE_SIGMA_FACTOR = 5.0`).

Node counts come from the **precision level**
(`pde_configuration.vanilla_precision`), mapping to `_custom_n_s` (-> `J`) and
`_custom_n_t` (-> `N`) via the constants in `src/tasks/pricer.hpp`:

| `vanilla_precision` | `n_s` (J) | `n_t` (N) |
|---------------------|-----------|-----------|
| `low`               | 501       | 301       |
| `medium`            | 1001      | 601       |
| `high` (default)    | 1501      | 1301      |

`custom_n_s` / `custom_n_t` override the level explicitly.

**Important — this is not a local-vol solver.** The whole grid uses one vol:

```cpp
v = Ctr->GetUnderlying()->GetImplicitVol( 0, maturity );  // ATM, this maturity
```

`v` is the ATM implied vol, read once and held constant across `X` and `t`. A
SABR (or any smile) surface therefore reaches the PDE engine **only through its
ATM level** — skew/smile shape is invisible to it. Compare against the MCL engine,
which samples the full surface. See [volatility.md](volatility.md).

## 3. Boundaries, American exercise and barriers

### Dirichlet boundaries

The two ends carry Dirichlet conditions `V_dw = Ctr->PDE_EvalFlow(X_min)` and
`V_up = Ctr->PDE_EvalFlow(X_max)` (the payoff evaluated at the domain edges),
pinned every timestep. The terminal layer `U_1` is initialised from
`PDE_EvalFlow(Phi(i*h))` over the grid.

### American early exercise

American handling is a straight **explicit projection** (not a true PSOR
iteration): after each tridiagonal solve, `SolveGrid` clamps the freshly solved
layer to the intrinsic value,

```cpp
U_0->data[k] = max( U_0->data[k], Ctr->PDE_EvalFlow( Phi(k*h) ) );
```

This is the standard one-pass projected step, applied per timestep when
`is_american` (= `Ctr->PDE_IsAmerican()`).

### Barriers

`PriceContract` routes barrier contracts (`InitGrid(Ctr, true)`):

- **Continuous monitoring** — the live-side boundary is moved onto the barrier
  and held at zero: up-barrier sets `X_max = H, V_up = 0`; down-barrier sets
  `X_min = H, V_dw = 0` (a knock-out Dirichlet clamp on the truncated domain).
- **Discrete monitoring** — the full domain is kept; the knocked-side boundary
  is held at 0, and the knocked region is zeroed **only at the scheduled steps**.
  `InitGrid` maps each fixing date to the nearest step `round(YearFraction/k)`
  into `_discrete_monitor_steps`; `SolveGrid` calls `ApplyDiscreteBarrier(U)` at
  those steps (and at terminal step `N`), zeroing every node with
  `Phi(jh) >= H` (up) or `<= H` (down).
- **Already breached at valuation** — knock-out is worth 0, knock-in equals the
  vanilla.
- **Knock-in via in/out parity** — there is no separate knock-in solve:
  `knock_in = vanilla - knock_out`, computed by running the clamped knock-out
  solve and a plain vanilla solve and subtracting premium, delta and gamma.

## 4. Greeks

There are two distinct Greek paths:

- **Off-grid (inside `SolveGrid`)** — premium, delta and gamma are read directly
  from the solved layer by central differences in `X` around `X_0` with a half-bump
  `h = X_0 * GREEK_SPOT_SHIFT / 2` (`GREEK_SPOT_SHIFT = 0.01`), evaluated by
  spline interpolation. These populate the `GridResult` returned by every solve.
- **Bump-and-revalue (book-level)** — `GreeksPerContract()` returns `true`, so
  `PriceBook` runs `PriceBookByContract`, and when Greeks are requested
  `ComputeContractGreeks` re-solves the grid under bumped markets:
  delta/gamma by spot bumps, vega by a vol bump, rho by a rate bump, theta by
  rolling `_today` one day. Note the ordering in `ComputeContractGreeks`: it
  reprices once more to restore the base premium, **then** overwrites delta/gamma
  with its own bumped values — so when Greeks are requested, the reported
  delta/gamma are the **bump-and-revalue** numbers, not the off-grid ones. The
  off-grid delta/gamma are what you get when only the premium (no Greeks) is
  requested.

## 5. The 2-D Heston extension (Douglas-ADI)

`PriceContract` routes a non-barrier vanilla to `SolveHestonGrid` when
`UnderlyingIsHeston` is true — i.e. the underlying is a **mono** (single-name) set
whose volatility `IsStochastic()`. The solved PDE is

```
V_t + 0.5 v S^2 V_SS + rho xi v S V_Sv + 0.5 xi^2 v V_vv
    + b S V_S + kappa(theta - v) V_v - r_d V = 0
```

with `b = log(F/S0)/T` the carry from the forward and `r_d` the premium-currency
discount; the Heston parameters `v0, kappa, theta, xi, rho` come from the
`HestonVolatility` object.

The scheme is **Douglas ADI** with directional implicitness `q = 0.5`:

1. an explicit full-operator predictor `Y0 = U + dt (A0 + A1 + A2) U`;
2. an **implicit S-sweep** `(I - q dt A1) Y1 = Y0 - q dt A1 U` for each `v`-line;
3. an **implicit v-sweep** `(I - q dt A2) U = Y1 - q dt A2 U` for each `S`-line.

The **cross term `A0` is explicit** (kept in the predictor only). Boundaries:
`S=0` Dirichlet `intrinsic0 * exp(-r_d tau)`, `S=Smax` linear extrapolation,
`v=Vmax` Neumann; at `v=0` the diffusion drops and an upwind `kappa*theta`
forward drift is used. American payoffs apply the same intrinsic projection after
each step. The price (and S-direction delta/gamma) are read by bilinear
interpolation at `(S0, v0)`. Tridiagonal solves use a local `Thomas` routine.

**Limitation — the Heston grid dimensions are hard-coded** in `SolveHestonGrid`
and ignore `vanilla_precision`:

```cpp
const int NS = 120;   // S nodes
const int Nv = 60;    // v nodes
const int Nt = 120;   // time steps
```

with extents `vmax = max(1, 6*max(v0,theta))` and
`smax = max(3*S0, S0*exp(5*sqrt(max(v0,theta)*max(T,1))))`. Only plain vanillas
on a mono Heston underlying are routed here; Heston barriers are not supported by
this path. See [volatility.md](volatility.md) for the model.

## 6. Quanto drift handling

`InitGrid` separates two rates: the **carry** `r` (drift, in the underlying
currency) and the **discount** `r_disc` (premium currency). They coincide for a
non-quanto contract. When the payoff currency differs from the underlying
currency, a quanto carry correction is applied:

```cpp
r -= rho * v * v_fx;   // r -> r - rho(S,FX) * sigma_S * sigma_FX
```

where `rho` is the signed FX-underlying correlation (`_correlation->GetValue`),
`v_fx` the FX vol (`GetFxVol`) and `sigma_S` the ATM implied vol `v`. Discounting
stays in the premium currency (`r_disc`). A quanto contract without a correlation
matrix is an error. This matches the ANA quanto forward and the MCL node so the
three engines agree. Currency identity is by pointer (Currency objects are
singletons), so the quanto test is exact; the `sigma_S = v` choice is exact under
a flat BS surface and an approximation under local/stochastic vol.

## Configuration example

```yaml
cfg_pde: !pricer_configuration
  method: pde
  pde_configuration: pde_cfg

pde_cfg: !pde_configuration
  vanilla_precision: high      # low | medium | high (default high)
  # custom_n_s: 2001           # optional explicit S-grid size (overrides level)
  # custom_n_t: 1601           # optional explicit time steps  (overrides level)
  # custom_sigma_factor: 5.0   # X_max/X_min = spot*exp(+/- factor*v*sqrt(T))
```

(`samples/matrix.yaml` and `samples/matrix.yaml` exercise the full matrix:
European/American vanillas, continuous/discrete barriers, quanto/compo and a
Heston vanilla.)

## Notes & limitations

- Single ATM vol across the grid: the 1-D solver is **not** a local-vol engine; a
  smile reaches it only through its ATM level.
- American exercise is a one-pass intrinsic projection, not iterated PSOR.
- The Heston ADI grid is hard-coded (`NS=120, Nv=60, Nt=120`) and ignores the
  precision level; it covers vanillas only.

## See also

- [running.md](running.md) — run wrappers and CLI modes
- [monte_carlo.md](monte_carlo.md) — the MCL engine (full-surface alternative)
- [volatility.md](volatility.md) — vol surfaces and the ATM-only caveat
- [products.md](products.md) — instruments and payoffs (`PDE_EvalFlow`)
- [gpu.md](gpu.md) — GPU offload
- [agent.md](agent.md) — agent mandate and conventions
