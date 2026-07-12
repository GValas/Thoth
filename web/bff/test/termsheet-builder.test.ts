import { describe, it, expect } from 'vitest';
import {
  buildTermsheetDoc,
  CONTRACT_NAME,
  TERMSHEET_RESULT_NAME,
  TERMSHEET_TASK_NAME,
  TAG_KEY,
  type TermsheetRequest,
} from '@thoth/shared';

//! the !termsheet-task document builder: no pricer, no book — the task, the
//! contract and the workspace's market objects, ready to dump to YAML.
describe('buildTermsheetDoc', () => {
  const req: TermsheetRequest = {
    today: '2000-01-01',
    instrument: {
      kind: 'vanilla',
      fields: { underlying: 'eq', strike: 100, maturity: '2000-12-31', type: 'call', exercise: 'european', premium_currency: 'eur' },
    },
    title: '1y call on eq',
    issuer: 'Demo Ltd',
  };
  const support = [
    { name: 'eq', kind: 'equity', payload: { spot: 100, volatility: 'vol', currency: 'eur' } },
    { name: 'vol', kind: 'bs_volatility', payload: { volatility: 20 } },
  ];

  it('roots the document at the termsheet task over the single contract', () => {
    const doc = buildTermsheetDoc(req, support) as Record<string, any>;
    expect(doc.root).toBe(TERMSHEET_TASK_NAME);
    const task = doc[TERMSHEET_TASK_NAME];
    expect(task[TAG_KEY]).toBe('termsheet');
    expect(task.contract).toBe(CONTRACT_NAME);
    expect(task.today).toBe('2000-01-01');
    expect(task.result).toBe(TERMSHEET_RESULT_NAME);
    expect(task.title).toBe('1y call on eq');
    expect(task.issuer).toBe('Demo Ltd');
  });

  it('tags the contract by its kind and passes fields through verbatim', () => {
    const doc = buildTermsheetDoc(req, support) as Record<string, any>;
    const contract = doc[CONTRACT_NAME];
    expect(contract[TAG_KEY]).toBe('vanilla');
    expect(contract.strike).toBe(100);
    expect(contract.premium_currency).toBe('eur');
  });

  it('merges the support objects and synthesises neither a pricer nor a book', () => {
    const doc = buildTermsheetDoc(req, support) as Record<string, any>;
    expect(doc.eq[TAG_KEY]).toBe('equity');
    expect(doc.vol[TAG_KEY]).toBe('bs_volatility');
    const tags = Object.values(doc)
      .filter((v): v is Record<string, unknown> => typeof v === 'object' && v !== null)
      .map((v) => v[TAG_KEY]);
    expect(tags).not.toContain('book');
    expect(tags.some((t) => typeof t === 'string' && (t as string).endsWith('_pricer'))).toBe(false);
  });

  it('omits the optional title/issuer lines when absent', () => {
    const doc = buildTermsheetDoc({ ...req, title: undefined, issuer: undefined }, support) as Record<string, any>;
    expect('title' in doc[TERMSHEET_TASK_NAME]).toBe(false);
    expect('issuer' in doc[TERMSHEET_TASK_NAME]).toBe(false);
  });
});
