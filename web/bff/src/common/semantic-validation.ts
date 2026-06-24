//! Semantic validation beyond JSON Schema. ajv checks shape/enums/types per object,
//! but the Thoth schema does NOT encode: (a) curve dates/values equal length, (b) that
//! a reference names an existing object. These are the classic config errors, so the
//! BFF enforces them before pricing. Pure functions (no Nest) so they're easy to test.

import type { SchemaService } from '../schema/schema.service';

export interface WsObject {
  name: string;
  kind: string;
  payload: Record<string, unknown>;
}

//! curve-like kinds and the two array fields that must pair up.
const PAIRED_ARRAYS: Record<string, [string, string]> = {
  yield_curve: ['dates', 'values'],
  repo_curve: ['dates', 'values'],
  continuous_dividends_curve: ['dates', 'values'],
  discrete_dividends: ['dates', 'amounts'],
  simple_fixing_data: ['dates', 'values'],
};

//! Validate ONE object's payload: ajv shape + curve pairing + that its references
//! resolve within `known` (the set of object names available in the workspace).
export function validateObject(
  schema: SchemaService,
  obj: WsObject,
  known: ReadonlySet<string>,
): string[] {
  const errors: string[] = [];

  if (!schema.hasKind(obj.kind)) {
    return [`unknown kind '${obj.kind}'`];
  }

  // (1) shape / enums / required (ajv)
  errors.push(...schema.validateObject(obj.kind, obj.payload).map((e) => `schema: ${e}`));

  // (2) paired arrays of equal length (schema does not enforce this)
  const pair = PAIRED_ARRAYS[obj.kind];
  if (pair) {
    const [a, b] = pair;
    const av = obj.payload[a];
    const bv = obj.payload[b];
    if (Array.isArray(av) && Array.isArray(bv) && av.length !== bv.length) {
      errors.push(`'${a}' (${av.length}) and '${b}' (${bv.length}) must have equal length`);
    }
  }

  // (3) references must resolve to an existing object name
  const { single, list } = schema.refFields(obj.kind);
  for (const f of single) {
    const v = obj.payload[f];
    if (typeof v === 'string' && v.length > 0 && !known.has(v)) {
      errors.push(`reference '${f}: ${v}' names no object in the workspace`);
    }
  }
  for (const f of list) {
    const v = obj.payload[f];
    if (Array.isArray(v)) {
      for (const item of v) {
        if (typeof item === 'string' && !known.has(item)) {
          errors.push(`reference '${f}' -> '${item}' names no object in the workspace`);
        }
      }
    }
  }

  return errors;
}

//! Validate a whole set (each object against the names present in the set).
export function validateObjects(schema: SchemaService, objects: ReadonlyArray<WsObject>): Record<string, string[]> {
  const known = new Set(objects.map((o) => o.name));
  const out: Record<string, string[]> = {};
  for (const o of objects) {
    const errs = validateObject(schema, o, known);
    if (errs.length) out[o.name] = errs;
  }
  return out;
}
