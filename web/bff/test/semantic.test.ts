import { describe, it, expect, beforeAll } from 'vitest';
import { SchemaService } from '../src/schema/schema.service';
import { validateObject, validateObjects } from '../src/common/semantic-validation';

function makeSchema(): SchemaService {
  const svc = new SchemaService({ get: (_k: string, def: string) => def } as any);
  svc.onModuleInit();
  return svc;
}

describe('semantic validation (beyond ajv)', () => {
  let schema: SchemaService;
  beforeAll(() => {
    schema = makeSchema();
  });

  it('flags curve dates/values length mismatch (schema does not)', () => {
    const errs = validateObject(
      schema,
      { name: 'r', kind: 'yield_curve', payload: { dates: ['2026-01-01', '2027-01-01'], values: [8] } },
      new Set(['r']),
    );
    expect(errs.some((e) => e.includes('equal length'))).toBe(true);
  });

  it('accepts an equal-length curve', () => {
    const errs = validateObject(
      schema,
      { name: 'r', kind: 'yield_curve', payload: { dates: ['2026-01-01', '2027-01-01'], values: [8, 8] } },
      new Set(['r']),
    );
    expect(errs).toEqual([]);
  });

  it('flags a dangling reference', () => {
    const errs = validateObject(
      schema,
      { name: 'eq', kind: 'equity', payload: { spot: 100, volatility: 'missing_vol', currency: 'eur' } },
      new Set(['eq', 'eur']), // 'missing_vol' absent
    );
    expect(errs.some((e) => e.includes("names no object"))).toBe(true);
  });

  it('accepts resolvable references across a set', () => {
    const map = validateObjects(schema, [
      { name: 'eq', kind: 'equity', payload: { spot: 100, volatility: 'vol', currency: 'eur' } },
      { name: 'vol', kind: 'bs_volatility', payload: { volatility: 30 } },
      { name: 'eur', kind: 'currency', payload: { rate: 'r' } },
      { name: 'r', kind: 'yield_curve', payload: { dates: ['2026-01-01', '2031-01-01'], values: [5, 5] } },
    ]);
    expect(map).toEqual({});
  });

  it('flags an unknown kind', () => {
    const errs = validateObject(schema, { name: 'x', kind: 'nope', payload: {} }, new Set(['x']));
    expect(errs[0]).toContain('unknown kind');
  });
});
