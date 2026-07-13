# Supported products

Reference for the instruments and underlyings Thoth can price, the YAML kinds and
fields that build them, and which pricing engine supports which combination.

Field names below are the exact keys read by the object registry
(`src/core/object_registry.cpp`); enum strings are the ones parsed in
`src/helpers/enums.hpp` / `src/helpers/constants.hpp`. Engine support is derived
from each contract's `*_HasSolution` / `GPU_GbmParams` methods in
`src/contracts/`.

## Instruments

A contract is a YAML node tagged with its kind. Every contract shares two common
fields, resolved by `ConfigureContractCommon`:

| field | meaning |
| --- | --- |
| `underlying` | name of the underlying object (equity / composite / basket / rainbow) |
| `premium_currency` | currency the premium is reported and settled in |

> `is_absolute_strike` and `nominal` appear in some sample books for readability
> but are **not** parsed by the registry — they are inert. The strike is always
> the absolute `strike` value; relative outputs are derived at report time
> (`Contract::RelativeFactor`, premium rebased to 100 / spot).

### `vanilla` — European / American call / put

`src/contracts/vanilla.{hpp,cpp}`.

| field | values | notes |
| --- | --- | --- |
| `strike` | double | absolute strike |
| `maturity` | date | single payment / fixing date |
| `type` | `call` / `put` | `OptionType` |
| `exercise` | `european` / `american` | `ExerciseMode` |

European is closed-form (Black–Scholes, or the Heston/Bates characteristic
function when the underlying carries a Heston vol). American is early-exercisable
and priced numerically only.

### `barrier` — single knock-in / knock-out

`src/contracts/barrier.{hpp,cpp}`. Terminal payoff is the vanilla one; the
knock condition is applied by the engine.

| field | values | notes |
| --- | --- | --- |
| `strike` | double | |
| `maturity` | date | |
| `type` | `call` / `put` | |
| `barrier_type` | `up&out` / `up&in` / `down&out` / `down&in` | `BarrierType` |
| `barrier_monitoring_type` | `continuous_monitoring` / `discrete_monitoring` | `BarrierMonitoring` |
| `monitoring_period_days` | integer (default `0`) | discrete step in days; schedule is `today + k·period` up to and including maturity |
| `barrier_up_level` | double (default `0`) | used for up barriers |
| `barrier_down_level` | double (default `0`) | used for down barriers |

Continuous monitoring has a Reiner–Rubinstein closed form (knock-out via in/out
parity). Discrete monitoring is **not** closed-form: it is priced numerically
(MCL monitors exactly at the scheduled dates; the PDE zeroes the knocked region
at each monitoring date).

### `variance` — realized-variance swap

`src/contracts/variance.{hpp,cpp}`. Pays
`notional · (realized_variance − strike_variance)` at maturity.

| field | values | notes |
| --- | --- | --- |
| `maturity` | date | single payment |
| `volatility_strike` | double, **in percent** | stored as a decimal (`/100`); strike variance is its square |
| `notional` | double (default `1`) | variance notional |

The analytic price is a model-free static-replication strip
(Demeterfi–Derman–Kamal–Zou) over the implied surface, so a smile feeds in. The
payoff is path-dependent in the spot, so there is **no PDE grid** for it.

### Contract YAML example

```yaml
my_call: !vanilla
  underlying: eq
  premium_currency: eur
  strike: 100
  maturity: 2000-12-31
  type: call
  exercise: european

up_out: !barrier
  underlying: eq
  premium_currency: eur
  strike: 100
  maturity: 2000-12-31
  type: call
  barrier_type: up&out
  barrier_monitoring_type: discrete_monitoring
  monitoring_period_days: 30
  barrier_up_level: 130

var_swap: !variance
  underlying: eq
  premium_currency: eur
  maturity: 2000-12-31
  volatility_strike: 30      # percent
  notional: 1
```

## Underlyings

An underlying is the diffusing object a contract references. Its kind drives
which engine the contract can use (see the matrix).

### `equity` — single asset (mono)

`src/underlyings/mono.hpp`, wrapping `Equity`. A single spot + vol + currency,
optionally repo and continuous dividends.

| field | values | notes |
| --- | --- | --- |
| `spot` | double | |
| `volatility` | name | `bs_volatility` / `sabr_volatility` / `heston_volatility` |
| `currency` | name | quotation currency |
| `continuous_dividends` | name (optional) | `continuous_dividends_curve` |
| `repo` | name (optional) | `repo_curve` |

### `composite` — quanto

`src/underlyings/composite.{hpp,cpp}`. An equity quoted in one currency but
settled in another; the quanto drift correction `F·exp(−ρ·σ_S·σ_X·t)` is applied.

| field | values | notes |
| --- | --- | --- |
| `equity` | name | the underlying equity |
| `composite_currency` | name | settlement currency |

### `basket` — weighted basket (`AbsoluteBasket`)

`src/underlyings/absolute_basket.{hpp,cpp}`. A weighted sum of member spots,
each diffused on its own local vol.

| field | values | notes |
| --- | --- | --- |
| `underlyings` | list of names | member underlyings |
| `weights` | list of doubles | one weight per member |

### `rainbow` — best-of / worst-of

`src/underlyings/rainbow.{hpp,cpp}`. Spot is the max (best-of) or min (worst-of)
of the members' rebased performances scaled to 100. Because the payoff is on a
max/min — not a single lognormal — it is **Monte-Carlo only**.

| field | values | notes |
| --- | --- | --- |
| `underlyings` | list of names | member underlyings |
| `type` | `best_of` / `worst_of` | `RainbowType` |

> For `basket` and `rainbow` the contract's `premium_currency` is forced onto the
> underlying (its rebased spot is dimensionless).

## Engine-support matrix

Engines: **ANA** (closed form), **PDE** (finite-difference grid), **MCL**
(CPU Monte-Carlo), **mcl_gpu** (GPU Monte-Carlo). `mcl_gpu` runs a single-asset
European GBM kernel only; for anything else it **falls back to the CPU MCL
engine** (`GPU_GbmParams` returns false). Cells: ✅ supported / — not.

| product / exercise | ANA | PDE | MCL | mcl_gpu (native) |
| --- | :-: | :-: | :-: | :-: |
| vanilla European | ✅ | ✅ | ✅ | ✅ (equity GBM only) |
| vanilla American | — | ✅ | ✅ | — (→ CPU) |
| vanilla (Heston / Bates vol) | ✅ (char. fn.) | ✅ | ✅ | — (→ CPU) |
| barrier, continuous monitoring | ✅ (Reiner–Rubinstein) | ✅ | ✅ | — (→ CPU) |
| barrier, discrete monitoring | — | ✅ | ✅ | — (→ CPU) |
| variance | ✅ (replication) | — | ✅ | — (→ CPU) |

Underlying restrictions (independent of the product):

| underlying | ANA | PDE | MCL | mcl_gpu (native) |
| --- | :-: | :-: | :-: | :-: |
| equity (mono) | ✅ | ✅ | ✅ | ✅ (European vanilla, deterministic vol) |
| composite (quanto) | ✅ | ✅ | ✅ | — (→ CPU) |
| basket | ✅ | ✅ | ✅ | — (→ CPU) |
| rainbow (best/worst-of) | — | — | ✅ | — (→ CPU) |

Notes derived from the code:

- **American** (`vanilla` with `exercise: american`): `ANA_HasSolution` requires
  European, so ANA rejects it; `PDE_IsAmerican` and the MCL flow node both
  support it.
- **Variance swap**: `PDE_HasSolution` returns false (path-dependent in spot);
  priced by ANA or MCL.
- **Rainbow**: `GetForward` / `GetImplicitVol` reject the single-forward
  assumption, so only MCL prices it.
- **Quanto** (`composite`): the same drift correction is wired across ANA, PDE
  and MCL, so the three agree (including American quanto via PDE / MCL).
- **mcl_gpu** native path requires: European exercise, an `equity` (mono)
  underlying, and a deterministic implied vol (not Heston). All other cases
  silently use the CPU MCL engine, so results are unchanged.

## Greeks

Greeks are requested per pricer via the `indicators` list field. Recognised
names (`src/tasks/pricer.cpp`):

| indicator | meaning | how computed |
| --- | --- | --- |
| `premium` | present value | engine price (rebased relative premium also available) |
| `delta` | dPV / dspot | per-underlying relative spot bump (1%) |
| `gamma` | d²PV / dspot² | wider relative spot bump (10%), central |
| `vega` | dPV / 1 vol point | absolute vol bump (0.01) |
| `rho` | dPV / 1% parallel rate | absolute rate bump (1 bp) |
| `theta` | dPV over one calendar day | re-date bump |

```yaml
my_pricer: !mcl_pricer
  indicators: [premium, delta, gamma, vega, rho, theta]
```

Delta and gamma are accumulated per contract; vega, rho and theta are computed at
the pricer (book) level by bump-and-revalue. The analytic vanilla also fills the
closed-form BS delta/gamma/vega/volga; the variance swap fills an analytic
vol-sensitivity (`dPV/dvol`).

## See also

- [running.md](running.md) — running pricers and books
- [monte_carlo.md](monte_carlo.md) — the MCL / mcl_gpu engines
- [pde.md](pde.md) — the finite-difference engine
- [volatility.md](volatility.md) — BS / SABR / Heston volatility models
- [gpu.md](gpu.md) — the GPU Monte-Carlo kernel and fallback
- [agent.md](agent.md) — the agent mandate
