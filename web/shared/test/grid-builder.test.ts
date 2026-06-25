import { describe, it, expect } from 'vitest';
import {
  buildGridDoc,
  parseGridResult,
  cellName,
  engineHasPerCellGreeks,
  type GridContext,
} from '../src/grid-builder.js';
import { TAG_KEY, type GridRequest } from '../src/index.js';

const req: GridRequest = {
  engine: 'ana',
  today: '2026-06-24',
  currency: 'eur',
  underlyings: ['eq'],
  types: ['call', 'put'],
  strikes: [90, 100, 110],
  maturities: ['2026-12-24', '2027-06-24'],
  indicators: ['premium', 'delta', 'vega'],
};

const ctx: GridContext = { pricerName: 'grid', resultName: 'grid_result', supportObjects: [] };

describe('grid builder', () => {
  it('builds one vanilla per (underlying,type,strike,maturity)', () => {
    const doc = buildGridDoc(req, ctx);
    const cells = Object.keys(doc).filter((k) => k.startsWith('cell__'));
    expect(cells.length).toBe(1 * 2 * 3 * 2); // 12
    const c = doc[cellName('eq', 'call', 0, 1, 1, 1)] as Record<string, unknown>;
    expect(c[TAG_KEY]).toBe('vanilla');
    expect(c.strike).toBe(90);
    expect(c.maturity).toBe('2027-06-24');
    expect(c.type).toBe('call');
    expect(c.exercise).toBe('european');
  });

  it('selects the pricer tag/config by engine', () => {
    const ana = buildGridDoc({ ...req, engine: 'ana' }, ctx).grid as Record<string, unknown>;
    expect((ana as any)[TAG_KEY]).toBe('ana_pricer');
    expect((ana as any).pde_configuration).toBeUndefined();

    const pde = buildGridDoc({ ...req, engine: 'pde' }, { ...ctx, configName: 'pd' }).grid as any;
    expect(pde[TAG_KEY]).toBe('pde_pricer');
    expect(pde.pde_configuration).toBe('pd');

    const mcl = buildGridDoc({ ...req, engine: 'mcl' }, { ...ctx, configName: 'm' }).grid as any;
    expect(mcl[TAG_KEY]).toBe('mcl_pricer');
    expect(mcl.mcl_configuration).toBe('m');
  });

  it('synthesises a default engine config when none is provided (GUI path)', () => {
    // PDE: a !pde_configuration object is created and referenced.
    const pdeDoc = buildGridDoc({ ...req, engine: 'pde' }, ctx);
    const pdeName = (pdeDoc.grid as any).pde_configuration as string;
    expect(typeof pdeName).toBe('string');
    expect((pdeDoc[pdeName] as any)[TAG_KEY]).toBe('pde_configuration');
    expect((pdeDoc[pdeName] as any).vanilla_precision).toBe('medium');

    // MCL: a !mcl_configuration object with the required fields is created and referenced.
    const mclDoc = buildGridDoc({ ...req, engine: 'mcl' }, ctx);
    const mclName = (mclDoc.grid as any).mcl_configuration as string;
    const cfg = mclDoc[mclName] as any;
    expect(cfg[TAG_KEY]).toBe('mcl_configuration');
    expect(cfg.paths).toBeGreaterThan(1);
    expect(cfg.max_day_step).toBeGreaterThan(0);

    // ANA never gets a config object.
    const anaDoc = buildGridDoc({ ...req, engine: 'ana' }, ctx);
    expect((anaDoc.grid as any).pde_configuration).toBeUndefined();
    expect((anaDoc.grid as any).mcl_configuration).toBeUndefined();
  });

  it('auto-attaches the workspace correlation matrix (MCL mandates one)', () => {
    const withCorrel: GridContext = {
      ...ctx,
      supportObjects: [{ name: 'correl', kind: 'correlation_matrix', payload: {} }],
    };
    const mcl = buildGridDoc({ ...req, engine: 'mcl' }, withCorrel).grid as any;
    expect(mcl.correlation).toBe('correl');
    // an explicit correlationName still wins
    const explicit = buildGridDoc({ ...req, engine: 'mcl' }, { ...withCorrel, correlationName: 'c2' }).grid as any;
    expect(explicit.correlation).toBe('c2');
  });

  it('pivots per-cell results into matrices', () => {
    // fake a result block: premium = i*10 + j, delta = 0.5 for every call cell
    const block: Record<string, number> = {};
    for (let i = 0; i < 3; i++)
      for (let j = 0; j < 2; j++) {
        block[`${cellName('eq', 'call', i, j, 1, 1)}_premium`] = i * 10 + j;
        block[`${cellName('eq', 'call', i, j, 1, 1)}_delta`] = 0.5;
        block[`${cellName('eq', 'put', i, j, 1, 1)}_premium`] = 100 + i * 10 + j;
      }
    const mats = parseGridResult(block, req);
    expect(mats.length).toBe(2); // (eq,call) and (eq,put)
    const call = mats.find((m) => m.type === 'call')!;
    expect(call.premium[2][1]).toBe(21);
    expect(call.greeks.delta![0][0]).toBe(0.5);
    expect(call.greeks.vega).toBeDefined(); // requested but absent in block -> NaN-filled
    expect(Number.isNaN(call.greeks.vega![0][0])).toBe(true);
  });

  it('every engine (incl. CPU mcl) now exposes per-cell Greeks', () => {
    for (const engine of ['ana', 'pde', 'mcl', 'mcl_gpu'] as const) {
      expect(engineHasPerCellGreeks(engine)).toBe(true);
    }
    // requested Greeks become NaN-filled matrices (populated once the engine returns them)
    const mats = parseGridResult({}, { ...req, engine: 'mcl' });
    expect(mats[0].greeks.delta).toBeDefined();
  });

  it('mcl_gpu is an mcl_pricer with allow_gpu set, and has per-cell Greeks', () => {
    expect(engineHasPerCellGreeks('mcl_gpu')).toBe(true);
    const doc = buildGridDoc({ ...req, engine: 'mcl_gpu' }, ctx);
    expect((doc.grid as any)[TAG_KEY]).toBe('mcl_pricer'); // same tag as cpu mcl
    const cfgName = (doc.grid as any).mcl_configuration as string;
    const cfg = doc[cfgName] as any;
    expect(cfg[TAG_KEY]).toBe('mcl_configuration');
    expect(cfg.allow_gpu).toBe(true);
  });
});
