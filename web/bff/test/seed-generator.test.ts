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

  it('emits a strictly positive-definite correlation matrix (engine requires SPD)', () => {
    //! reconstruct the full matrix from the lower triangle and confirm a Cholesky factorisation
    //! succeeds (all pivots > 0) — a raw random unit-diagonal matrix usually fails this, so this
    //! guards the eigenvalue-clip repair in the generator across several seeds and member counts.
    const choleskyOk = (m: number[][]): boolean => {
      const n = m.length;
      const L = Array.from({ length: n }, () => new Array(n).fill(0));
      for (let i = 0; i < n; i++) {
        for (let j = 0; j <= i; j++) {
          let s = m[i][j];
          for (let k = 0; k < j; k++) s -= L[i][k] * L[j][k];
          if (i === j) {
            if (s <= 1e-12) return false; // not positive-definite
            L[i][i] = Math.sqrt(s);
          } else {
            L[i][j] = s / L[j][j];
          }
        }
      }
      return true;
    };
    //! broad sweep: the emitted entries are ROUNDED to 1e-4 after the eigenvalue-clip
    //! repair, a perturbation of up to ~n·5e-5 on the spectrum — the repair floor must
    //! dominate it or a clipped seed emits an indefinite matrix the engine rejects. On
    //! random n=10 unit-diagonal matrices with these entry ranges, a 1e-6 floor left
    //! ~34% of repaired-then-rounded matrices indefinite; the 1e-3 floor left none.
    for (let seed = 1; seed <= 500; seed++) {
      const objs = generateMarketData({ equities: 5, currencies: 6, seed });
      const correl = objs.find((o) => o.kind === 'correlation_matrix')!;
      const order = [
        ...(correl.payload.underlyings as string[]),
        ...(correl.payload.forexs as string[]),
      ];
      const n = order.length;
      const tri = correl.payload.symmetric_matrix as number[];
      const full = Array.from({ length: n }, () => new Array(n).fill(0));
      let k = 0;
      for (let i = 0; i < n; i++) for (let j = 0; j <= i; j++) full[i][j] = full[j][i] = tri[k++];
      expect(choleskyOk(full), `seed ${seed}`).toBe(true);
    }
  });

  it('is deterministic for a given seed', () => {
    expect(generateMarketData({ seed: 42 })).toEqual(generateMarketData({ seed: 42 }));
  });

  it('builds 10-pillar yearly curves from the workspace valuation date', () => {
    const objs = generateMarketData({ today: '2030-06-15' });
    for (const kind of ['yield_curve', 'repo_curve', 'continuous_dividends_curve']) {
      const curve = objs.find((o) => o.kind === kind)!;
      const dates = curve.payload.dates as string[];
      const values = curve.payload.values as number[];
      expect(dates).toHaveLength(10);
      expect(values).toHaveLength(10);
      expect(dates[0]).toBe('2030-06-15'); // first pillar = today
      expect(dates[9]).toBe('2039-06-15'); // last pillar = today + 9y
    }
  });
});
