//! Tag-preserving YAML for Thoth books.
//!
//! Thoth declares every object's kind with a local YAML tag (`name: !equity {...}`),
//! and js-yaml's default schema rejects unknown `!local` tags. The engine only ever
//! puts a tag on a TOP-LEVEL MAPPING (verified across pricer/samples: no nested-object,
//! sequence-element or scalar tags), so a single per-kind mapping Type is sufficient.
//!
//! In JS a tagged object is a plain object with a `__tag` field (TAG_KEY). The core
//! path is EMIT (JSON+kind -> `!kind` YAML for pricing); LOAD is only for importing
//! sample books. Engine RESULT blocks are untagged (they carry a `kind:` field, not a
//! tag), so loading leaves them as ordinary objects — exactly what we want.

import yaml from 'js-yaml';
import { TAG_KEY, type TaggedObject } from './types.js';

//! Build a js-yaml schema with one mapping Type per kind. The kind list is injected
//! (the BFF derives it from pricer/schema/thoth.schema.json $defs; tests pass a subset)
//! so this stays in sync with the schema rather than hardcoding a second source.
export function taggedSchema(kinds: readonly string[]): yaml.Schema {
  const types = kinds.map(
    (kind) =>
      new yaml.Type('!' + kind, {
        kind: 'mapping',
        //! load: wrap the mapping as { __tag: kind, ...fields }
        construct: (data: Record<string, unknown> | null) => ({
          [TAG_KEY]: kind,
          ...(data ?? {}),
        }),
        //! dump: only objects carrying this exact __tag emit this tag
        predicate: (data: unknown): data is TaggedObject =>
          typeof data === 'object' && data !== null && (data as TaggedObject)[TAG_KEY] === kind,
        //! strip the discriminator before emitting the mapping body
        represent: (data: object) => {
          const { [TAG_KEY]: _omit, ...rest } = data as TaggedObject;
          return rest;
        },
      }),
  );
  return yaml.DEFAULT_SCHEMA.extend(types);
}

//! Parse a Thoth YAML book into a JS object tree, wrapping each tagged mapping as
//! { __tag, ...fields }. Untagged mappings (result/system blocks) pass through plain.
export function loadBook(text: string, kinds: readonly string[]): unknown {
  return yaml.load(text, { schema: taggedSchema(kinds) });
}

//! Serialize a JS object tree to a Thoth YAML book, emitting `!kind` for every object
//! carrying a __tag. `sortKeys` keeps output stable for tests/diffs.
export function dumpBook(value: unknown, kinds: readonly string[]): string {
  return yaml.dump(value, { schema: taggedSchema(kinds), sortKeys: true, lineWidth: -1 });
}

//! Convenience: assemble a flat book `{ root, [name]: {__tag,...}, ... }` from stored
//! objects (name + kind + payload) and a root task name, then emit tagged YAML.
export function buildBookYaml(
  rootTask: string,
  objects: ReadonlyArray<{ name: string; kind: string; payload: Record<string, unknown> }>,
  kinds: readonly string[],
): string {
  const doc: Record<string, unknown> = { root: rootTask };
  for (const o of objects) {
    doc[o.name] = { [TAG_KEY]: o.kind, ...o.payload };
  }
  return dumpBook(doc, kinds);
}
