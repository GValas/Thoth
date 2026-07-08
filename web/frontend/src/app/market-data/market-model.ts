//! Bidirectional bridge between the flat WsObject[] the BFF stores and the four
//! dashboard areas (equities / rates / fx / correlation). The store wraps a signal of
//! objects and exposes grouped views + mutators; the pure helpers below build per-kind
//! defaults and the correlation view-model (which auto-resizes over the live members,
//! including the `<equity>_var` pseudo-underlying that carries a stochastic-vol equity's
//! spot/vol correlation — matrix.yaml convention).

import { computed, signal } from '@angular/core';
import type { WsObject } from '../core/models';

export const VOL_KINDS = [
  'bs_volatility',
  'sabr_volatility',
  'heston_volatility',
  'lsv_volatility',
] as const;
export const DIV_KINDS = ['continuous_dividends_curve', 'discrete_dividends'] as const;

//! Kinds the dashboard renders natively; anything else falls to the "Advanced" editor.
export const DASHBOARD_KINDS = new Set<string>([
  'equity',
  'currency',
  'yield_curve',
  'repo_curve',
  'continuous_dividends_curve',
  'discrete_dividends',
  'forex',
  'correlation_matrix',
  ...VOL_KINDS,
]);

export function addYears(date: string, years: number): string {
  const [y, m, d] = date.split('-');
  return `${(Number(y) + years).toString().padStart(4, '0')}-${m}-${d}`;
}

export function round(x: number, p = 4): number {
  return Math.round(x * 10 ** p) / 10 ** p;
}

//! Jacobi eigenvalue decomposition of a small symmetric n×n matrix (A = V·diag(d)·Vᵀ),
//! eigenvectors as columns of V.
function jacobiEigen(aIn: number[][]): { d: number[]; V: number[][] } {
  const n = aIn.length;
  const a = aIn.map((r) => [...r]);
  const V: number[][] = Array.from({ length: n }, (_, i) =>
    Array.from({ length: n }, (_, j) => (i === j ? 1 : 0)),
  );
  for (let sweep = 0; sweep < 100; sweep++) {
    let off = 0;
    for (let p = 0; p < n; p++) for (let q = p + 1; q < n; q++) off += a[p][q] * a[p][q];
    if (off < 1e-14) break;
    for (let p = 0; p < n; p++) {
      for (let q = p + 1; q < n; q++) {
        if (Math.abs(a[p][q]) < 1e-18) continue;
        const theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
        const t = Math.sign(theta || 1) / (Math.abs(theta) + Math.sqrt(theta * theta + 1));
        const c = 1 / Math.sqrt(t * t + 1);
        const s = t * c;
        for (let k = 0; k < n; k++) {
          const akp = a[k][p];
          const akq = a[k][q];
          a[k][p] = c * akp - s * akq;
          a[k][q] = s * akp + c * akq;
        }
        for (let k = 0; k < n; k++) {
          const apk = a[p][k];
          const aqk = a[q][k];
          a[p][k] = c * apk - s * aqk;
          a[q][k] = s * apk + c * aqk;
        }
        for (let k = 0; k < n; k++) {
          const vkp = V[k][p];
          const vkq = V[k][q];
          V[k][p] = c * vkp - s * vkq;
          V[k][q] = s * vkp + c * vkq;
        }
      }
    }
  }
  return { d: a.map((_, i) => a[i][i]), V };
}

//! Project a symmetric matrix onto the nearest valid correlation matrix: clip eigenvalues to
//! a small positive floor (strictly positive-definite, as the engine requires), reconstruct,
//! then rescale to a unit diagonal. A valid matrix is a near-fixed point, so applying this on
//! every edit only nudges the values when an edit has actually broken positive-definiteness.
//!
//! The 1e-3 floor must DOMINATE the 1e-4 rounding writeCorrel applies afterwards: rounding
//! perturbs the spectrum by up to n·5e-5, so a tiny floor (1e-6) could leave the emitted
//! matrix indefinite and the engine would reject it. Keep in sync with the same algorithm in
//! web/bff/src/common/correlation-math.ts and web/spot-feed/src/correl.ts.
export function repairCorrelation(m: number[][]): number[][] {
  const n = m.length;
  if (n === 0) return m;
  const { d, V } = jacobiEigen(m);
  const dc = d.map((x) => Math.max(x, 1e-3));
  const out = Array.from({ length: n }, () => Array.from({ length: n }, () => 0));
  for (let i = 0; i < n; i++) {
    for (let j = i; j < n; j++) {
      let s = 0;
      for (let k = 0; k < n; k++) s += V[i][k] * dc[k] * V[j][k];
      out[i][j] = out[j][i] = s;
    }
  }
  const inv = out.map((_, i) => 1 / Math.sqrt(out[i][i] || 1));
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) out[i][j] *= inv[i] * inv[j];
    out[i][i] = 1;
  }
  return out;
}

//! sensible starting payload when a vol object is created or its kind is switched.
export function defaultVol(kind: string, spot = 100): Record<string, unknown> {
  switch (kind) {
    case 'bs_volatility':
      return { volatility: 20 };
    case 'heston_volatility':
      return { spot, init_vol: 20, long_vol: 20, kappa: 1, vol_of_vol: 0.5 };
    case 'lsv_volatility':
      //! surface: must be filled with the name of a deterministic vol object
      return { spot, init_vol: 20, long_vol: 20, kappa: 1, vol_of_vol: 0.5, surface: '' };
    case 'sabr_volatility':
      return { maturities: [1], alpha: [0.2], beta: [1], rho: [0], nu: [0.4] };
    default:
      return {};
  }
}

export function defaultCurve(today: string, value = 1): { dates: string[]; values: number[] } {
  return { dates: [today, addYears(today, 10)], values: [value, value] };
}

//! Signal-backed store of the workspace's objects, with grouped views and mutators.
export class MarketModel {
  private readonly _objects = signal<WsObject[]>([]);
  readonly objects = this._objects.asReadonly();

  readonly equities = computed(() => this._objects().filter((o) => o.kind === 'equity'));
  readonly currencies = computed(() => this._objects().filter((o) => o.kind === 'currency'));
  readonly forexs = computed(() => this._objects().filter((o) => o.kind === 'forex'));
  readonly correlation = computed(
    () => this._objects().find((o) => o.kind === 'correlation_matrix') ?? null,
  );
  readonly advanced = computed(() => this._objects().filter((o) => !DASHBOARD_KINDS.has(o.kind)));

  set(objs: WsObject[]): void {
    this._objects.set(objs.map((o) => ({ ...o, payload: structuredClone(o.payload) })));
    this.syncForex();
    this.syncCorrel();
  }

  //! FX pairs are derived, not hand-added: keep exactly one !forex per non-pivot currency.
  //! They must form a *pivot basis* — every pair shares the same underlying (pivot) currency,
  //! the first currency — because the engine's correlation triangle requires it (see
  //! Correlation::SetForexList) and the seed generator uses the same convention. So the pair
  //! is named "<pivot>/<x>" with underlying_currency = pivot, base_currency = x. Creates
  //! missing pairs (+ their flat vol), drops stale ones; existing spot & vol are preserved.
  syncForex(): void {
    const ccys = this.currencyNames();
    //! Pivot = the underlying shared by the existing fx basis when there is one, NOT just the
    //! first currency: the backend returns objects sorted by name, so `currencyNames()[0]` is
    //! the alphabetically-first currency, which need not be the pivot the data was built with.
    //! Keying off the existing pairs' underlying keeps the basis stable across reloads (otherwise
    //! every reload renames the pairs and orphans the correlation matrix's `forexs`). Falls back
    //! to the first currency only to bootstrap a workspace that has no fx pair yet.
    const pivot = (this.forexs()[0]?.payload['underlying_currency'] as string) ?? ccys[0];
    const desired = new Map<string, string>(); // pair name -> base (quote) currency
    if (pivot) for (const x of ccys) if (x !== pivot) desired.set(`${pivot}/${x}`, x);

    for (const fx of this.forexs()) {
      if (!desired.has(fx.name)) {
        const vol = fx.payload['volatility'];
        this.remove(fx.name, ...(typeof vol === 'string' ? [vol] : []));
      }
    }
    for (const [name, x] of desired) {
      if (this.has(name)) continue;
      const volName = `${name}_vol`;
      if (!this.has(volName)) {
        this.upsert({ name: volName, kind: 'bs_volatility', payload: defaultVol('bs_volatility') });
      }
      this.upsert({
        name,
        kind: 'forex',
        payload: { base_currency: x, underlying_currency: pivot, spot: 1, volatility: volName },
      });
    }
  }
  //! Re-key the stored correlation matrix onto the current members so it can never reference a
  //! renamed/removed object (which the engine and the validator reject). Only rewrites when the
  //! member set actually drifted — existing correlations are carried over, new pairs default to
  //! 0 — so a consistent book is left untouched (no churn). Must run after syncForex().
  private syncCorrel(): void {
    const correl = this.correlation();
    if (!correl) return;
    const want = correlMembers(this);
    const have = [
      ...((correl.payload['underlyings'] as string[]) ?? []),
      ...((correl.payload['forexs'] as string[]) ?? []),
    ];
    if (want.length === have.length && want.every((m, i) => m === have[i])) return;
    writeCorrel(this, buildCorrelModel(this));
  }
  all(): WsObject[] {
    return this._objects();
  }
  byName(name: string): WsObject | undefined {
    return this._objects().find((o) => o.name === name);
  }
  has(name: string): boolean {
    return this._objects().some((o) => o.name === name);
  }
  currencyNames(): string[] {
    return this.currencies().map((c) => c.name);
  }

  //! merge fields into an object's payload
  patch(name: string, patch: Record<string, unknown>): void {
    this._objects.update((list) =>
      list.map((o) => (o.name === name ? { ...o, payload: { ...o.payload, ...patch } } : o)),
    );
  }
  //! replace an object's whole payload (and optionally its kind, e.g. vol-kind switch)
  replace(name: string, payload: Record<string, unknown>, kind?: string): void {
    this._objects.update((list) =>
      list.map((o) => (o.name === name ? { name, kind: kind ?? o.kind, payload } : o)),
    );
  }
  upsert(obj: WsObject): void {
    this._objects.update((list) => {
      const i = list.findIndex((o) => o.name === obj.name);
      if (i < 0) return [...list, obj];
      const next = [...list];
      next[i] = obj;
      return next;
    });
  }
  remove(...names: string[]): void {
    const drop = new Set(names);
    this._objects.update((list) => list.filter((o) => !drop.has(o.name)));
  }

  //! unique name with a numeric suffix (e.g. eq1, eq2 …)
  freshName(prefix: string): string {
    let i = 1;
    while (this.has(`${prefix}${i}`)) i++;
    return `${prefix}${i}`;
  }

  //! add a new equity with its 1:1 owned flat-vol / repo / continuous-dividend objects.
  addEquity(today: string): string {
    const name = this.freshName('eq');
    this.upsert({ name: `${name}_vol`, kind: 'bs_volatility', payload: defaultVol('bs_volatility') });
    this.upsert({ name: `${name}_repo`, kind: 'repo_curve', payload: defaultCurve(today, 0) });
    this.upsert({
      name: `${name}_div`,
      kind: 'continuous_dividends_curve',
      payload: defaultCurve(today, 0),
    });
    this.upsert({
      name,
      kind: 'equity',
      payload: {
        spot: 100,
        volatility: `${name}_vol`,
        currency: this.currencyNames()[0] ?? 'eur',
        repo: `${name}_repo`,
        continuous_dividends: `${name}_div`,
      },
    });
    return name;
  }
}

//! ---- correlation view-model ------------------------------------------------------------

//! Ordered members of the correlation matrix: each equity (then its `_var` pseudo-asset if
//! it uses a stochastic vol — Heston/Bates or LSV), followed by the fx pairs.
export function correlMembers(model: MarketModel): string[] {
  const members: string[] = [];
  for (const eq of model.equities()) {
    members.push(eq.name);
    const vol = model.byName(eq.payload['volatility'] as string);
    if (vol?.kind === 'heston_volatility' || vol?.kind === 'lsv_volatility')
      members.push(`${eq.name}_var`);
  }
  for (const fx of model.forexs()) members.push(fx.name);
  return members;
}

//! Read a stored correlation object into a name-keyed lookup (handles both `matrix` and
//! `symmetric_matrix`). Unknown pairs default to 0 (1 on the diagonal).
function readCorrel(obj: WsObject | null): (a: string, b: string) => number {
  if (!obj) return (a, b) => (a === b ? 1 : 0);
  const order = [
    ...((obj.payload['underlyings'] as string[]) ?? []),
    ...((obj.payload['forexs'] as string[]) ?? []),
  ];
  const n = order.length;
  const full: number[][] = Array.from({ length: n }, () => Array.from({ length: n }, () => 0));
  const matrix = obj.payload['matrix'] as number[] | undefined;
  const tri = obj.payload['symmetric_matrix'] as number[] | undefined;
  if (Array.isArray(matrix) && matrix.length === n * n) {
    for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) full[i][j] = matrix[i * n + j];
  } else if (Array.isArray(tri)) {
    let k = 0;
    for (let i = 0; i < n; i++)
      for (let j = 0; j <= i; j++) {
        full[i][j] = full[j][i] = tri[k++] ?? 0;
      }
  }
  const idx = new Map(order.map((m, i) => [m, i]));
  return (a, b) => {
    const ia = idx.get(a);
    const ib = idx.get(b);
    if (ia === undefined || ib === undefined) return a === b ? 1 : 0;
    return full[ia][ib];
  };
}

export interface CorrelModel {
  members: string[];
  matrix: number[][];
}

//! Build the current N×N matrix over the live members, carrying over any previously-set
//! correlations (so adding/removing an equity or switching to Heston resizes in place).
export function buildCorrelModel(model: MarketModel): CorrelModel {
  const members = correlMembers(model);
  const get = readCorrel(model.correlation());
  const matrix = members.map((a, i) =>
    members.map((b, j) => (i === j ? 1 : round(get(a, b)))),
  );
  return { members, matrix };
}

//! Fold an edited matrix back into the `correl` object (emitted as a full row-major matrix).
export function writeCorrel(model: MarketModel, cm: CorrelModel): void {
  const fxNames = new Set(model.forexs().map((f) => f.name));
  const underlyings = cm.members.filter((m) => !fxNames.has(m));
  const forexs = cm.members.filter((m) => fxNames.has(m));
  //! project the edited matrix back onto the valid (positive-definite, unit-diagonal) set so a
  //! single off-diagonal edit can never make the engine reject `correl` as not SPD.
  const repaired = repairCorrelation(cm.matrix).map((row) => row.map((x) => round(x)));
  const payload = { underlyings, forexs, matrix: repaired.flat() };
  if (model.correlation()) model.replace('correl', payload, 'correlation_matrix');
  else model.upsert({ name: 'correl', kind: 'correlation_matrix', payload });
}
