import { describe, it, expect, beforeAll } from 'vitest';
import { readFileSync, readdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { loadBook, TAG_KEY } from '@thoth/shared';
import { SchemaService } from '../src/schema/schema.service';

//! Guard against schema/engine drift — the dashboard hard-depends on their agreement.
//! 1. every kind the engine registers (KIND_* in object.hpp) is in the schema, and
//!    vice-versa; 2. every maintained input sample validates against the schema.

const pricer = resolve(__dirname, '../../../pricer');

function makeSchema(): SchemaService {
  const cfg = { get: (_k: string, def: string) => def } as any; // SCHEMA_PATH default is relative to cwd
  const svc = new SchemaService(cfg);
  svc.onModuleInit();
  return svc;
}

//! The kinds the engine DISPATCHES via its object registry — the authoritative set of
//! top-level objects a config can declare. Built by mapping each KIND_* constant
//! referenced in object_registry.cpp to its string value (scanned from all headers).
//! (The schema legitimately also covers contextually-parsed tags like
//! simple_weighted_calendar that are not registry objects — so the invariant is
//! registry ⊆ schema, not equality.)
function registryKinds(): string[] {
  const headers = ['src/core/object.hpp'];
  const nameToValue = new Map<string, string>();
  for (const h of headers) {
    const text = readFileSync(resolve(pricer, h), 'utf8');
    for (const m of text.matchAll(/(KIND_[A-Z_]+)\[\]\s*=\s*"([a-z_]+)"/g)) {
      nameToValue.set(m[1], m[2]);
    }
  }
  const registry = readFileSync(resolve(pricer, 'src/core/object_registry.cpp'), 'utf8');
  const referenced = new Set([...registry.matchAll(/\{\s*(KIND_[A-Z_]+)\s*,/g)].map((m) => m[1]));
  return [...referenced].map((n) => nameToValue.get(n)).filter((v): v is string => !!v).sort();
}

describe('schema <-> engine drift guard', () => {
  let schema: SchemaService;
  beforeAll(() => {
    schema = makeSchema();
  });

  it('every engine-dispatched (registry) kind is covered by the schema', () => {
    const reg = registryKinds();
    expect(reg.length).toBeGreaterThan(20); // sanity: the parse found the registry
    const schemaKinds = new Set(schema.kinds());
    const missing = reg.filter((k) => !schemaKinds.has(k));
    expect(missing, `engine kinds absent from the schema: ${missing.join(', ')}`).toEqual([]);
  });

  it('every maintained input sample validates against the schema', () => {
    const kinds = schema.kinds();
    const samples = readdirSync(resolve(pricer, 'samples')).filter(
      (f) => f.endsWith('.yaml') && !f.endsWith('.out.yaml'),
    );
    expect(samples.length).toBeGreaterThan(0);
    for (const file of samples) {
      const doc = loadBook(readFileSync(resolve(pricer, 'samples', file), 'utf8'), kinds) as Record<string, any>;
      for (const [name, value] of Object.entries(doc)) {
        if (name === 'root' || typeof value !== 'object' || value === null || !(TAG_KEY in value)) continue;
        const { [TAG_KEY]: kind, ...payload } = value;
        const errs = schema.validateObject(kind, payload);
        expect(errs, `${file}:${name} (!${kind}) -> ${errs.join('; ')}`).toEqual([]);
      }
    }
  });
});
