//! Pure book synthesis for the MCP tools: turn the flat, LLM-friendly tool
//! arguments (spot / vol / rate / instrument fields) into a self-contained Thoth
//! YAML book, and pivot the engine's result block back into a flat JSON record.
//! No I/O here — everything is unit-testable; the engine call lives in index.ts.

import {
  buildInstrumentDoc,
  dumpBook,
  loadBook,
  parseInstrumentResult,
  CONTRACT_NAME,
  type Engine,
  type InstrumentContext,
  type InstrumentRequest,
} from '@thoth/shared';

//! per-maturity SABR parameters (decimals; e.g. alpha 0.3 = 30% ATM-ish for beta 1)
export interface SabrArgs {
  alpha: number;
  beta: number;
  rho: number;
  nu: number;
  maturities?: number[]; //!< pillar maturities in years; defaults to [10] (flat in time)
}

//! the market every pricing tool shares: one equity in one currency, flat curves.
//! Percent conventions follow the engine (vol 30 = 30%, rate 5 = 5%/y).
export interface MarketArgs {
  today: string; //!< valuation date, YYYY-MM-DD
  spot: number;
  vol_pct?: number; //!< flat Black-Scholes vol in percent (XOR sabr)
  sabr?: SabrArgs; //!< SABR smile (arbitrage-free wings) instead of a flat vol
  rate_pct: number; //!< flat continuously-compounded zero rate in percent
  dividend_pct?: number; //!< flat continuous dividend yield in percent
  repo_pct?: number; //!< flat repo spread in percent
}

//! engine selection + the Monte-Carlo knobs that matter for a one-off quote
export interface EngineArgs {
  engine: Engine; //!< 'ana' | 'pde' | 'mcl'
  paths?: number; //!< MCL paths (default 100000)
  max_day_step?: number; //!< MCL diffusion step in days (default 7)
}

const CCY = 'eur';
const FAR_DATE_YEARS = 30; //!< flat curves span [today, today + 30y]

function addYears(date: string, years: number): string {
  const [y, m, d] = date.split('-');
  return `${(Number(y) + years).toString().padStart(4, '0')}-${m}-${d}`;
}

//! the supporting market objects (currency, curves, vol, equity, correlation)
//! synthesized from the flat tool args — every reference resolves by construction.
export function buildMarketObjects(
  m: MarketArgs,
): Array<{ name: string; kind: string; payload: Record<string, unknown> }> {
  if ((m.vol_pct === undefined) === (m.sabr === undefined)) {
    throw new Error("provide exactly one of 'vol_pct' (flat) or 'sabr' (smile)");
  }
  const far = addYears(m.today, FAR_DATE_YEARS);
  const flatCurve = (v: number) => ({ dates: [m.today, far], values: [v, v] });

  const objs: Array<{ name: string; kind: string; payload: Record<string, unknown> }> = [
    { name: `${CCY}_rate`, kind: 'yield_curve', payload: flatCurve(m.rate_pct) },
    { name: CCY, kind: 'currency', payload: { rate: `${CCY}_rate` } },
    { name: 'cal', kind: 'simple_weighted_calendar', payload: { non_working_days_weight: 1 } },
    { name: 'cor', kind: 'correlation_matrix', payload: { underlyings: ['eq'], matrix: [1] } },
  ];

  if (m.sabr !== undefined) {
    const s = m.sabr;
    const pillars = s.maturities ?? [10];
    objs.push({
      name: 'vol',
      kind: 'sabr_volatility',
      payload: {
        maturities: pillars,
        alpha: pillars.map(() => s.alpha),
        beta: pillars.map(() => s.beta),
        rho: pillars.map(() => s.rho),
        nu: pillars.map(() => s.nu),
        calendar: 'cal',
      },
    });
  } else {
    objs.push({
      name: 'vol',
      kind: 'bs_volatility',
      payload: { volatility: m.vol_pct as number, calendar: 'cal' },
    });
  }

  const equity: Record<string, unknown> = { spot: m.spot, volatility: 'vol', currency: CCY };
  if (m.dividend_pct !== undefined) {
    objs.push({ name: 'div', kind: 'continuous_dividends_curve', payload: flatCurve(m.dividend_pct) });
    equity['continuous_dividends'] = 'div';
  }
  if (m.repo_pct !== undefined) {
    objs.push({ name: 'rep', kind: 'repo_curve', payload: flatCurve(m.repo_pct) });
    equity['repo'] = 'rep';
  }
  objs.push({ name: 'eq', kind: 'equity', payload: equity });
  return objs;
}

//! one-instrument book: the shared instrument-builder with the synthesized market.
//! `fields` are the contract's own fields (underlying/premium_currency filled here).
export function buildBook(
  m: MarketArgs,
  e: EngineArgs,
  kind: string,
  fields: Record<string, unknown>,
  indicators: string[],
  kinds: readonly string[],
): string {
  const req: InstrumentRequest = {
    engine: e.engine,
    today: m.today,
    currency: CCY,
    indicators,
    instrument: {
      kind,
      fields: { underlying: 'eq', premium_currency: CCY, ...fields },
    },
  };
  const ctx: InstrumentContext = {
    pricerName: 'pricer',
    resultName: 'res',
    supportObjects: buildMarketObjects(m),
  };
  const doc = buildInstrumentDoc(req, ctx);
  //! MCL knobs: the builder synthesizes a default mcl_configuration; override the
  //! knobs a quote actually cares about (paths / step) when provided
  if (e.engine === 'mcl') {
    const cfg = doc['_instrument_engine_config'] as Record<string, unknown>;
    if (e.paths !== undefined) cfg['paths'] = e.paths;
    if (e.max_day_step !== undefined) cfg['max_day_step'] = e.max_day_step;
  }
  return dumpBook(doc, kinds);
}

//! flat result record from the engine's YAML reply: premium, standard error (MCL)
//! and whichever Greeks were requested. Throws if the result block is absent.
export function parseBook(
  resultYaml: string,
  indicators: string[],
  kinds: readonly string[],
): Record<string, number> {
  const doc = loadBook(resultYaml, kinds) as Record<string, unknown>;
  const block = doc['res'] as Record<string, unknown> | undefined;
  if (!block) {
    throw new Error('engine reply carries no result block');
  }
  const parsed = parseInstrumentResult(block, indicators);
  const out: Record<string, number> = { premium: parsed.premium };
  const trust = block[`${CONTRACT_NAME}_premium_trust`];
  if (typeof trust === 'number' && trust > 0) out['premium_std_error'] = trust;
  for (const [g, v] of Object.entries(parsed.greeks)) out[g] = v as number;
  return out;
}
