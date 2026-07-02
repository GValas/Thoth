//! Semantic validation beyond JSON Schema. ajv checks shape/enums/types per object,
//! but the Thoth schema does NOT encode: (a) curve dates/values equal length, (b) that
//! a reference names an existing object, (c) curve dates strictly increasing, (d) the
//! correlation matrix being symmetric positive-definite with a unit diagonal. (c) and
//! (d) are hard engine rejects (ERR "strictly croissant" / "not symmetric
//! positive-definite") that would otherwise only surface as a late pricing failure.
//! Pure functions (no Nest) so they're easy to test.

import { fullFromFlat, fullFromTriangle, isPositiveDefinite } from './correlation-math';
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

//! Validate a correlation_matrix payload the way the engine will read it: the member
//! list is [underlyings..., forexs...]; `matrix` is full row-major n², or
//! `symmetric_matrix` the lower triangle (diagonal included, n(n+1)/2). The engine
//! hard-rejects a non-SPD matrix; a non-unit diagonal or |entry| > 1 is economically
//! wrong for a correlation, so both are flagged here too.
function validateCorrelation(payload: Record<string, unknown>): string[] {
  const errors: string[] = [];
  const members = [
    ...(Array.isArray(payload.underlyings) ? (payload.underlyings as unknown[]) : []),
    ...(Array.isArray(payload.forexs) ? (payload.forexs as unknown[]) : []),
  ];
  const n = members.length;
  if (n === 0) return errors; // ajv/reference checks already cover a missing member list

  //! reconstruct the full matrix from either representation
  let full: number[][] | null = null;
  const flat = payload.matrix;
  const tri = payload.symmetric_matrix;
  if (Array.isArray(flat) && flat.every((x) => typeof x === 'number')) {
    full = fullFromFlat(flat as number[], n);
    if (!full) {
      return [`'matrix' has ${flat.length} entries, expected ${n * n} (${n} members squared)`];
    }
    for (let i = 0; i < n; i++) {
      for (let j = 0; j < i; j++) {
        if (Math.abs(full[i][j] - full[j][i]) > 1e-9) {
          errors.push(`'matrix' is not symmetric at (${i},${j}): ${full[i][j]} vs ${full[j][i]}`);
          return errors;
        }
      }
    }
  } else if (Array.isArray(tri) && tri.every((x) => typeof x === 'number')) {
    full = fullFromTriangle(tri as number[], n);
    if (!full) {
      return [`'symmetric_matrix' has ${tri.length} entries, expected ${(n * (n + 1)) / 2} (lower triangle of ${n} members)`];
    }
  }
  if (!full) return errors; // neither array present/numeric: ajv reports the shape error

  for (let i = 0; i < n; i++) {
    if (Math.abs(full[i][i] - 1) > 1e-9) {
      errors.push(`correlation diagonal must be 1 (member '${String(members[i])}' has ${full[i][i]})`);
    }
    for (let j = 0; j < i; j++) {
      if (Math.abs(full[i][j]) > 1) {
        errors.push(`correlation (${String(members[i])}, ${String(members[j])}) = ${full[i][j]} is outside [-1, 1]`);
      }
    }
  }
  if (errors.length === 0 && !isPositiveDefinite(full)) {
    errors.push('correlation matrix is not symmetric positive-definite (the engine rejects it)');
  }
  return errors;
}

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
    //! (2b) dates strictly increasing — the engine hard-rejects ties and inversions
    //! ("date_list must be strictly croissant"; equal pillars would divide by a zero
    //! year fraction). ISO YYYY-MM-DD strings compare correctly lexicographically.
    if (Array.isArray(av)) {
      for (let i = 1; i < av.length; i++) {
        const prev = av[i - 1];
        const cur = av[i];
        if (typeof prev === 'string' && typeof cur === 'string' && cur <= prev) {
          errors.push(`'${a}' must be strictly increasing ('${cur}' follows '${prev}')`);
          break;
        }
      }
    }
  }

  // (2c) correlation matrix: symmetric positive-definite, unit diagonal, entries in
  // [-1, 1] — the engine hard-rejects a non-SPD matrix at load.
  if (obj.kind === 'correlation_matrix') {
    errors.push(...validateCorrelation(obj.payload));
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
