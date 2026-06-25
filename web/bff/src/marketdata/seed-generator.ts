//! Random-but-valid market-data generator for the dashboard's "Generate sample data"
//! button. Produces a fully cross-consistent WsObject[] set — every reference resolves,
//! paired curve arrays are equal length, and only schema-allowed fields are emitted — so
//! the result passes the same ajv + semantic validation as hand-entered data
//! (see common/semantic-validation.ts). Pure (no Nest) and deterministic given `seed`,
//! so it is easy to unit-test.

import type { WsObject } from '../common/semantic-validation';

export interface SeedOptions {
  //! number of equities (default 5) and currencies (default 3) to spawn
  equities?: number;
  currencies?: number;
  //! valuation date (YYYY-MM-DD) — curves span 10 yearly pillars from today
  today?: string;
  //! PRNG seed for reproducible output
  seed?: number;
}

//! mulberry32 — a tiny deterministic PRNG so seeded data is stable (and testable).
function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

//! add `years` to a YYYY-MM-DD date string, keeping month/day.
function addYears(date: string, years: number): string {
  const [y, m, d] = date.split('-');
  const yy = (Number(y) + years).toString().padStart(4, '0');
  return `${yy}-${m}-${d}`;
}

//! a 10-pillar yearly curve from `today` out to +9y, the values drifting mildly around
//! `base` (a gentle term structure across the pillars + small per-pillar noise, clamped
//! non-negative) so the generated curves read as real term structures, not flat lines.
function curve10(
  today: string,
  base: number,
  rnd: () => number,
  p = 2,
): { dates: string[]; values: number[] } {
  const dates: string[] = [];
  const values: number[] = [];
  for (let i = 0; i < 10; i++) {
    dates.push(addYears(today, i));
    //! slope from 0.85*base (front) to 1.15*base (back), plus +/-5% noise
    const v = base * (0.85 + (0.3 * i) / 9) + (rnd() - 0.5) * base * 0.1;
    values.push(Math.round(Math.max(0, v) * 10 ** p) / 10 ** p);
  }
  return { dates, values };
}

const CCY_POOL = ['eur', 'usd', 'jpy', 'gbp', 'chf', 'cad'];

//! realistic large-cap tickers to draw equity names from (sampled, no repeats).
const STOCK_POOL = [
  'AAPL', 'MSFT', 'GOOGL', 'AMZN', 'NVDA', 'META', 'TSLA', 'JPM', 'V', 'JNJ',
  'WMT', 'XOM', 'NESN', 'ROG', 'NOVN', 'ASML', 'SAP', 'MC', 'OR', 'SIE',
  'TM', 'SONY', '7203', 'BABA', 'TSM', 'SHEL', 'HSBA', 'AZN', 'ULVR', 'BP',
];

//! Generate a coherent market-data set: currencies (+ yield curves), equities (each with a
//! 1:1 owned flat-BS vol, repo curve and continuous-dividend curve), fx pairs (+ their vol),
//! and a single correlation matrix over [equities..., fx...].
export function generateMarketData(opts: SeedOptions = {}): WsObject[] {
  const nEq = Math.max(1, Math.min(opts.equities ?? 5, 26));
  const nCcy = Math.max(1, Math.min(opts.currencies ?? 3, CCY_POOL.length));
  const today = opts.today ?? '2026-01-01';
  const rnd = mulberry32(opts.seed ?? 1);
  const round = (x: number, p = 2) => Math.round(x * 10 ** p) / 10 ** p;
  const between = (lo: number, hi: number, p = 2) => round(lo + rnd() * (hi - lo), p);

  const objs: WsObject[] = [];
  const ccys = CCY_POOL.slice(0, nCcy);

  // --- currencies: a !currency pointing at its own 10-pillar !yield_curve (rates in percent) ---
  for (const c of ccys) {
    objs.push({ name: `${c}_rate`, kind: 'yield_curve', payload: curve10(today, between(0.5, 5), rnd) });
    objs.push({ name: c, kind: 'currency', payload: { rate: `${c}_rate` } });
  }

  // --- equities: spot + 1:1 owned flat vol / repo curve / continuous-dividend curve ---
  // realistic, distinct tickers sampled from the pool (Fisher–Yates with the seeded PRNG)
  const pool = [...STOCK_POOL];
  for (let i = pool.length - 1; i > 0; i--) {
    const j = Math.floor(rnd() * (i + 1));
    [pool[i], pool[j]] = [pool[j], pool[i]];
  }
  const equityNames: string[] = [];
  for (let i = 0; i < nEq; i++) {
    const name = pool[i] ?? `STK${i + 1}`;
    equityNames.push(name);
    const ccy = ccys[Math.floor(rnd() * ccys.length)];
    objs.push({ name: `${name}_vol`, kind: 'bs_volatility', payload: { volatility: between(15, 45) } });
    objs.push({ name: `${name}_repo`, kind: 'repo_curve', payload: curve10(today, between(0, 1), rnd) });
    objs.push({
      name: `${name}_div`,
      kind: 'continuous_dividends_curve',
      payload: curve10(today, between(0, 3), rnd),
    });
    objs.push({
      name,
      kind: 'equity',
      payload: {
        spot: between(20, 200, 1),
        volatility: `${name}_vol`,
        currency: ccy,
        repo: `${name}_repo`,
        continuous_dividends: `${name}_div`,
      },
    });
  }

  // --- fx: a pivot basis — every pair shares the same *underlying* (pivot) currency,
  // currencies[0], quoted against each other currency; name = "pivot/x". The engine's
  // correlation triangle requires this common-underlying basis (Correlation::SetForexList
  // takes the pivot from the first pair and rejects any pair anchored elsewhere), so the
  // shared currency must be the underlying, not the base — cf. the canonical eur/usd sample.
  const fxNames: string[] = [];
  const pivot = ccys[0];
  for (const x of ccys.slice(1)) {
    const pair = `${pivot}/${x}`;
    fxNames.push(pair);
    objs.push({ name: `${pair}_vol`, kind: 'bs_volatility', payload: { volatility: between(5, 15) } });
    objs.push({
      name: pair,
      kind: 'forex',
      payload: {
        base_currency: x,
        underlying_currency: pivot,
        spot: between(0.5, 2, 4),
        volatility: `${pair}_vol`,
      },
    });
  }

  // --- correlation: one symmetric matrix over [equities..., fx...]; diagonal 1 ---
  const members = [...equityNames, ...fxNames];
  const n = members.length;
  const symmetric: number[] = []; // lower triangle, row-major: (0,0),(1,0),(1,1),(2,0)...
  for (let i = 0; i < n; i++) {
    for (let j = 0; j <= i; j++) {
      symmetric.push(i === j ? 1 : between(-0.2, 0.5));
    }
  }
  objs.push({
    name: 'correl',
    kind: 'correlation_matrix',
    payload: { underlyings: equityNames, forexs: fxNames, symmetric_matrix: symmetric },
  });

  return objs;
}
