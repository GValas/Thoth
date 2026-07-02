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

describe('semantic validation: curve dates ordering', () => {
  let schema: SchemaService;
  beforeAll(() => {
    schema = makeSchema();
  });

  it('flags equal adjacent pillar dates (engine rejects ties)', () => {
    const errs = validateObject(
      schema,
      {
        name: 'r',
        kind: 'yield_curve',
        payload: { dates: ['2026-01-01', '2026-01-01'], values: [5, 5] },
      },
      new Set(['r']),
    );
    expect(errs.some((e) => e.includes('strictly increasing'))).toBe(true);
  });

  it('flags decreasing pillar dates', () => {
    const errs = validateObject(
      schema,
      {
        name: 'd',
        kind: 'discrete_dividends',
        payload: { dates: ['2027-01-01', '2026-06-01'], amounts: [1, 1] },
      },
      new Set(['d']),
    );
    expect(errs.some((e) => e.includes('strictly increasing'))).toBe(true);
  });

  it('accepts strictly increasing dates', () => {
    const errs = validateObject(
      schema,
      {
        name: 'r',
        kind: 'yield_curve',
        payload: { dates: ['2026-01-01', '2027-01-01', '2028-01-01'], values: [5, 5, 5] },
      },
      new Set(['r']),
    );
    expect(errs).toEqual([]);
  });
});

describe('semantic validation: correlation matrix SPD', () => {
  let schema: SchemaService;
  beforeAll(() => {
    schema = makeSchema();
  });

  const correl = (payload: Record<string, unknown>) =>
    validateObject(schema, { name: 'cor', kind: 'correlation_matrix', payload }, new Set(['cor', 'a', 'b', 'c']));

  it('accepts a valid full matrix', () => {
    const errs = correl({ underlyings: ['a', 'b'], matrix: [1, 0.5, 0.5, 1] });
    expect(errs).toEqual([]);
  });

  it('accepts a valid lower-triangle symmetric_matrix', () => {
    const errs = correl({ underlyings: ['a', 'b', 'c'], symmetric_matrix: [1, 0.2, 1, 0.1, 0.3, 1] });
    expect(errs).toEqual([]);
  });

  it('flags a non-positive-definite matrix (rho grid the engine rejects)', () => {
    //! rho(a,b)=0.9, rho(a,c)=0.9, rho(b,c)=-0.9 is jointly infeasible
    const errs = correl({
      underlyings: ['a', 'b', 'c'],
      symmetric_matrix: [1, 0.9, 1, 0.9, -0.9, 1],
    });
    expect(errs.some((e) => e.includes('positive-definite'))).toBe(true);
  });

  it('flags an asymmetric full matrix', () => {
    const errs = correl({ underlyings: ['a', 'b'], matrix: [1, 0.5, 0.4, 1] });
    expect(errs.some((e) => e.includes('not symmetric'))).toBe(true);
  });

  it('flags a wrong-size matrix', () => {
    const errs = correl({ underlyings: ['a', 'b'], matrix: [1, 0.5, 0.5] });
    expect(errs.some((e) => e.includes('expected 4'))).toBe(true);
  });

  it('flags a non-unit diagonal and out-of-range entries', () => {
    const errs = correl({ underlyings: ['a', 'b'], matrix: [0.9, 0.5, 0.5, 1] });
    expect(errs.some((e) => e.includes('diagonal must be 1'))).toBe(true);
    const errs2 = correl({ underlyings: ['a', 'b'], symmetric_matrix: [1, 1.2, 1] });
    expect(errs2.some((e) => e.includes('outside [-1, 1]'))).toBe(true);
  });

  it('counts forexs into the member list', () => {
    //! 2 equities + 1 fx pair -> n=3, triangle size 6
    const errs = correl({
      underlyings: ['a', 'b'],
      forexs: ['c'],
      symmetric_matrix: [1, 0.2, 1, 0.1, 0.3, 1],
    });
    expect(errs).toEqual([]);
  });
});
