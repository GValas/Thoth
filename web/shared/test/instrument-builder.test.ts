import { describe, it, expect } from 'vitest';
import {
  buildInstrumentDoc,
  parseInstrumentResult,
  CONTRACT_NAME,
  type InstrumentContext,
  type InstrumentRequest,
} from '../src/instrument-builder.js';
import { TAG_KEY } from '../src/index.js';

const baseReq = (overrides: Partial<InstrumentRequest> = {}): InstrumentRequest => ({
  engine: 'ana',
  today: '2026-06-24',
  currency: 'eur',
  indicators: ['premium', 'delta', 'vega'],
  instrument: {
    kind: 'vanilla',
    fields: { underlying: 'eq', strike: 100, maturity: '2026-12-24', type: 'call', exercise: 'european' },
  },
  ...overrides,
});

const ctx: InstrumentContext = {
  pricerName: 'pricer',
  resultName: 'pricer_result',
  supportObjects: [],
};

describe('instrument builder', () => {
  it('builds a one-contract book tagged by the instrument kind', () => {
    const doc = buildInstrumentDoc(baseReq(), ctx);
    const contract = doc[CONTRACT_NAME] as Record<string, unknown>;
    expect(contract[TAG_KEY]).toBe('vanilla');
    expect(contract['underlying']).toBe('eq');
    expect(contract['strike']).toBe(100);

    const book = doc['instrument_book'] as Record<string, unknown>;
    expect(book[TAG_KEY]).toBe('book');
    expect(book['contracts']).toEqual([CONTRACT_NAME]);

    const pricer = doc[ctx.pricerName] as Record<string, unknown>;
    expect(pricer[TAG_KEY]).toBe('ana_pricer');
    expect(pricer['book']).toBe('instrument_book');
    expect(pricer['result']).toBe('pricer_result');
    expect(doc['root']).toBe('pricer');
  });

  it('passes barrier variations through verbatim', () => {
    const doc = buildInstrumentDoc(
      baseReq({
        engine: 'pde',
        instrument: {
          kind: 'barrier',
          fields: {
            underlying: 'eq',
            strike: 100,
            maturity: '2026-12-24',
            type: 'call',
            barrier_type: 'up&out',
            barrier_monitoring_type: 'continuous_monitoring',
            barrier_up_level: 120,
          },
        },
      }),
      ctx,
    );
    const contract = doc[CONTRACT_NAME] as Record<string, unknown>;
    expect(contract[TAG_KEY]).toBe('barrier');
    expect(contract['barrier_type']).toBe('up&out');
    expect(contract['barrier_up_level']).toBe(120);
    // pde synthesises a default engine config
    const pricer = doc[ctx.pricerName] as Record<string, unknown>;
    expect(pricer['pde_configuration']).toBe('_instrument_engine_config');
    expect(doc['_instrument_engine_config']).toBeTruthy();
  });

  it('auto-attaches a workspace correlation matrix when present', () => {
    const doc = buildInstrumentDoc(baseReq({ engine: 'mcl' }), {
      ...ctx,
      supportObjects: [{ name: 'cor', kind: 'correlation_matrix', payload: {} }],
    });
    const pricer = doc[ctx.pricerName] as Record<string, unknown>;
    expect(pricer['correlation']).toBe('cor');
    expect(pricer['mcl_configuration']).toBe('_instrument_engine_config');
  });

  it('pivots the result block into premium + requested greeks, omitting absent fields', () => {
    const block = {
      [`${CONTRACT_NAME}_premium`]: 12.5,
      [`${CONTRACT_NAME}_delta`]: 0.6,
      // no vega written by the engine
    };
    const res = parseInstrumentResult(block, ['premium', 'delta', 'vega']);
    expect(res.premium).toBe(12.5);
    expect(res.greeks.delta).toBe(0.6);
    expect(res.greeks.vega).toBeUndefined();
  });
});
