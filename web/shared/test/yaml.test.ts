import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { loadBook, dumpBook, buildBookYaml } from '../src/yaml.js';
import { TAG_KEY } from '../src/types.js';

const here = dirname(fileURLToPath(import.meta.url));
const schemaPath = resolve(here, '../../../pricer/schema/thoth.schema.json');

//! kinds = every $defs entry whose title is "!<kind>" — the same set the engine knows.
function schemaKinds(): string[] {
  const schema = JSON.parse(readFileSync(schemaPath, 'utf8'));
  return Object.values(schema.$defs as Record<string, { title?: string }>)
    .map((d) => d.title)
    .filter((t): t is string => typeof t === 'string' && t.startsWith('!'))
    .map((t) => t.slice(1));
}

const KINDS = schemaKinds();

describe('tag-preserving YAML', () => {
  it('emits !kind for tagged objects', () => {
    const doc = {
      root: 'p',
      eq: { [TAG_KEY]: 'equity', spot: 100, volatility: 'vol', currency: 'eur' },
    };
    const text = dumpBook(doc, KINDS);
    expect(text).toContain('!equity');
    expect(text).toContain('spot: 100');
    expect(text).not.toContain(TAG_KEY); // the discriminator must not leak into YAML
  });

  it('round-trips a synthetic book structurally (tag + fields preserved)', () => {
    const doc = {
      root: 'p',
      eq: { [TAG_KEY]: 'equity', spot: 100, volatility: 'vol', currency: 'eur' },
      vol: { [TAG_KEY]: 'bs_volatility', volatility: 30 },
    };
    const back = loadBook(dumpBook(doc, KINDS), KINDS);
    expect(back).toEqual(doc);
  });

  it('loads a real sample, wrapping tags as __tag', () => {
    const text = readFileSync(resolve(here, '../../../pricer/samples/simple_call.yaml'), 'utf8');
    const doc = loadBook(text, KINDS) as Record<string, any>;
    expect(doc.eq[TAG_KEY]).toBe('equity');
    expect(doc.eq.spot).toBe(100);
    expect(doc.simple_call_pricing[TAG_KEY]).toBe('mcl_pricer');
    // structural round-trip: dump then reload equals the first load
    const reloaded = loadBook(dumpBook(doc, KINDS), KINDS);
    expect(reloaded).toEqual(doc);
  });

  it('buildBookYaml assembles a flat tagged book from stored objects', () => {
    const yaml = buildBookYaml(
      'p',
      [{ name: 'eq', kind: 'equity', payload: { spot: 100, volatility: 'vol', currency: 'eur' } }],
      KINDS,
    );
    expect(yaml).toContain('root: p');
    expect(yaml).toMatch(/eq: !equity/);
  });
});
