//! Shared types for the Thoth engine integration layer.

//! A YAML object carrying a Thoth local tag (e.g. `!equity`). On the wire the tag is
//! `!<kind>`; in JS we represent it as a plain object with a `__tag` discriminator so
//! the same value round-trips through tag-preserving load/dump. Reference: the engine
//! reads the kind only from the tag (pricer/src/yaml_config.cpp GetTag).
export const TAG_KEY = '__tag' as const;

export interface TaggedObject {
  [TAG_KEY]: string; //!< the kind, without the leading '!'
  [field: string]: unknown;
}

//! Keys that must never appear in free-form, user-supplied object fields. `__proto__`,
//! `constructor` and `prototype` are prototype-pollution vectors; TAG_KEY is our internal
//! kind discriminator (a crafted `__tag` in user fields could otherwise override the
//! validated kind and make the engine price a different product than the one validated).
//! We strip these in every builder as defense-in-depth so no controller/DTO boundary is
//! the single line of defence.
const RESERVED_FIELD_KEYS: ReadonlySet<string> = new Set(['__proto__', 'constructor', 'prototype', TAG_KEY]);

//! Return a shallow copy of `fields` with the reserved keys removed. Used before spreading
//! any user-supplied field bag into a tagged object.
export function sanitizeFields(fields: Record<string, unknown>): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const key of Object.keys(fields)) {
    if (RESERVED_FIELD_KEYS.has(key)) continue;
    out[key] = fields[key];
  }
  return out;
}

//! The single Greek/premium fields a pricer writes per contract / per book. Per-cell
//! Greeks exist only on ANA/PDE/GPU-MCL (see pricer.cpp WriteResults gating); premium
//! and premium_trust are always present.
export interface CellResult {
  premium: number;
  premium_trust?: number;
  delta?: number;
  gamma?: number;
  vega?: number;
  rho?: number;
  theta?: number;
}

//! Process-global progress snapshot from GET /progress: "<current> <total> <active>".
//! Global, not per-request — correlation comes from leasing one engine replica per job.
export interface ProgressSnapshot {
  current: number;
  total: number;
  active: boolean;
}

//! Engine selection for a pricing book. Per-cell Greeks require ana | pde | mcl_gpu
//! (CPU mcl gives book-level Greeks only). `mcl_gpu` is the same !mcl_pricer as `mcl`
//! but with allow_gpu set, so it runs the device Monte-Carlo and emits per-contract Greeks.
export type Engine = 'ana' | 'pde' | 'mcl' | 'mcl_gpu';
export type OptionType = 'call' | 'put';
export type Exercise = 'european' | 'american';

//! A strike x maturity grid request (one product = vanilla in v1), one engine, one or
//! more underlyings and call/put types. indicators drive which Greeks are requested.
export interface GridRequest {
  engine: Engine;
  today: string; //!< YYYY-MM-DD
  currency: string; //!< reporting currency name (must exist among the objects)
  underlyings: string[]; //!< object names of the underlyings to price across
  types: OptionType[];
  strikes: number[];
  maturities: string[]; //!< YYYY-MM-DD
  indicators: string[]; //!< e.g. ["premium","delta","gamma","vega","rho","theta"]
  exercise?: Exercise; //!< default "european"
}

//! One (underlying, type) pivot: a rows(strikes) x cols(maturities) matrix per metric.
export interface GridMatrix {
  underlying: string;
  type: OptionType;
  currency: string; //!< contract premium currency (same for every cell of a grid)
  strikes: number[];
  maturities: string[];
  premium: number[][];
  greeks: Partial<Record<keyof CellResult, number[][]>>;
}
