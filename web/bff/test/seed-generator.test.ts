import { describe, it, expect, beforeAll } from 'vitest';
import { SchemaService } from '../src/schema/schema.service';
import { validateObjects } from '../src/common/semantic-validation';
import { generateMarketData } from '../src/marketdata/seed-generator';

function makeSchema(): SchemaService {
  const svc = new SchemaService({ get: (_k: string, def: string) => def } as any);
  svc.onModuleInit();
  return svc;
}

describe('market-data seed generator', () => {
  let schema: SchemaService;
  beforeAll(() => {
    schema = makeSchema();
  });

  it('produces a fully valid set (ajv + semantic) by default (5 eq / 3 ccy)', () => {
    const objs = generateMarketData();
    const errors = validateObjects(schema, objs);
    expect(errors).toEqual({});
  });

  it('generates the requested counts and the expected kinds', () => {
    const objs = generateMarketData({ equities: 5, currencies: 3 });
    const byKind = (k: string) => objs.filter((o) => o.kind === k);
    expect(byKind('equity')).toHaveLength(5);
    expect(byKind('currency')).toHaveLength(3);
    expect(byKind('yield_curve')).toHaveLength(3); // one per currency
    expect(byKind('repo_curve')).toHaveLength(5); // one per equity
    expect(byKind('continuous_dividends_curve')).toHaveLength(5);
    expect(byKind('forex')).toHaveLength(2); // currencies - 1, vs the base
    expect(byKind('correlation_matrix')).toHaveLength(1);
  });

  it('emits a correctly-sized lower-triangle correlation matrix over [equities, fx]', () => {
    const objs = generateMarketData({ equities: 4, currencies: 3 });
    const correl = objs.find((o) => o.kind === 'correlation_matrix')!;
    const n = 4 + 2; // equities + fx pairs
    const tri = (correl.payload.symmetric_matrix as number[]).length;
    expect(tri).toBe((n * (n + 1)) / 2);
    expect((correl.payload.underlyings as string[])).toHaveLength(4);
    expect((correl.payload.forexs as string[])).toHaveLength(2);
  });

  it('is deterministic for a given seed', () => {
    expect(generateMarketData({ seed: 42 })).toEqual(generateMarketData({ seed: 42 }));
  });

  it('honors the workspace valuation date in curve pillars', () => {
    const objs = generateMarketData({ today: '2030-06-15' });
    const curve = objs.find((o) => o.kind === 'yield_curve')!;
    expect((curve.payload.dates as string[])[0]).toBe('2030-06-15');
    expect((curve.payload.dates as string[])[1]).toBe('2040-06-15');
  });
});
