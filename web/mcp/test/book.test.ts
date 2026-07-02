import { describe, it, expect } from 'vitest';
import { loadBook } from '@thoth/shared';
import { buildBook, buildMarketObjects, parseBook } from '../src/book.js';

const KINDS = [
  'ana_pricer', 'pde_pricer', 'mcl_pricer', 'mcl_configuration', 'pde_configuration',
  'currency', 'yield_curve', 'repo_curve', 'continuous_dividends_curve',
  'simple_weighted_calendar', 'correlation_matrix', 'equity', 'bs_volatility',
  'sabr_volatility', 'book', 'vanilla', 'barrier', 'variance_swap',
];

const MARKET = { today: '2026-01-01', spot: 100, vol_pct: 30, rate_pct: 5 };

describe('buildMarketObjects', () => {
  it('synthesizes a fully-referenced flat market', () => {
    const objs = buildMarketObjects(MARKET);
    const names = new Set(objs.map((o) => o.name));
    for (const n of ['eur', 'eur_rate', 'cal', 'cor', 'vol', 'eq']) expect(names.has(n)).toBe(true);
    const eq = objs.find((o) => o.name === 'eq')!;
    expect(eq.payload).toMatchObject({ spot: 100, volatility: 'vol', currency: 'eur' });
    //! flat 2-pillar curve spanning 30y
    const rate = objs.find((o) => o.name === 'eur_rate')!;
    expect(rate.payload.dates).toEqual(['2026-01-01', '2056-01-01']);
    expect(rate.payload.values).toEqual([5, 5]);
  });

  it('wires optional dividend / repo curves onto the equity', () => {
    const objs = buildMarketObjects({ ...MARKET, dividend_pct: 2, repo_pct: 0.5 });
    const eq = objs.find((o) => o.name === 'eq')!;
    expect(eq.payload.continuous_dividends).toBe('div');
    expect(eq.payload.repo).toBe('rep');
  });

  it('builds a SABR surface when sabr is given (per-pillar parameter lists)', () => {
    const objs = buildMarketObjects({
      ...MARKET,
      vol_pct: undefined,
      sabr: { alpha: 0.3, beta: 1, rho: -0.3, nu: 0.4, maturities: [1, 5] },
    });
    const vol = objs.find((o) => o.name === 'vol')!;
    expect(vol.kind).toBe('sabr_volatility');
    expect(vol.payload).toMatchObject({
      maturities: [1, 5], alpha: [0.3, 0.3], beta: [1, 1], rho: [-0.3, -0.3], nu: [0.4, 0.4],
    });
  });

  it('rejects both or neither of vol_pct / sabr', () => {
    expect(() => buildMarketObjects({ ...MARKET, vol_pct: undefined })).toThrow(/exactly one/);
    expect(() =>
      buildMarketObjects({ ...MARKET, sabr: { alpha: 0.3, beta: 1, rho: 0, nu: 0.4 } }),
    ).toThrow(/exactly one/);
  });
});

describe('buildBook', () => {
  it('emits a parseable tagged book with the contract and pricer wired', () => {
    const yaml = buildBook(
      MARKET,
      { engine: 'ana' },
      'vanilla',
      { strike: 100, maturity: '2027-01-01', type: 'call', exercise: 'european' },
      ['premium', 'delta'],
      KINDS,
    );
    const doc = loadBook(yaml, KINDS) as Record<string, any>;
    expect(doc.root).toBe('pricer');
    expect(doc.pricer.__tag).toBe('ana_pricer');
    expect(doc.pricer.indicators).toEqual(['premium', 'delta']);
    expect(doc.contract).toMatchObject({
      __tag: 'vanilla', underlying: 'eq', premium_currency: 'eur', strike: 100, type: 'call',
    });
    expect(doc.instrument_book.contracts).toEqual(['contract']);
  });

  it('applies the MCL knobs onto the synthesized engine config', () => {
    const yaml = buildBook(
      MARKET,
      { engine: 'mcl', paths: 250000, max_day_step: 30 },
      'vanilla',
      { strike: 100, maturity: '2027-01-01', type: 'call', exercise: 'european' },
      ['premium'],
      KINDS,
    );
    const doc = loadBook(yaml, KINDS) as Record<string, any>;
    expect(doc._instrument_engine_config).toMatchObject({ paths: 250000, max_day_step: 30 });
  });
});

describe('parseBook', () => {
  it('pivots the result block to a flat record (premium + trust + greeks)', () => {
    const reply = [
      'res:',
      '  kind: pricer_result',
      '  contract_premium: 15.7',
      '  contract_premium_trust: 0.02',
      '  contract_delta: 0.67',
      '  premium: 15.7',
    ].join('\n');
    const out = parseBook(reply, ['premium', 'delta'], KINDS);
    expect(out).toEqual({ premium: 15.7, premium_std_error: 0.02, delta: 0.67 });
  });

  it('throws on a reply without a result block', () => {
    expect(() => parseBook('other: {}', ['premium'], KINDS)).toThrow(/no result block/);
  });
});
