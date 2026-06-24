//! Loads pricer/schema/thoth.schema.json once and exposes it three ways:
//!  - kinds(): the kind list (from each $def's "!kind" title) — feeds the YAML tag layer
//!    and the front's per-kind form branching.
//!  - defs(): the per-kind $defs (the front builds formly forms from these).
//!  - validateObject(kind, payload): ajv validation of a single object against its kind.
//!
//! The schema is hand-maintained in the engine repo (no generator), so a guard test
//! (schema.drift.test.ts) checks it against the engine's KIND_* registry and the samples.

import { Injectable, OnModuleInit, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import Ajv, { type ValidateFunction } from 'ajv';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

interface KindDef {
  title?: string;
  [k: string]: unknown;
}

@Injectable()
export class SchemaService implements OnModuleInit {
  private readonly log = new Logger(SchemaService.name);
  private schema!: { $defs: Record<string, KindDef> };
  private ajv!: Ajv;
  private kindToDefKey = new Map<string, string>(); //!< kind -> $defs key
  private validators = new Map<string, ValidateFunction>();

  constructor(private readonly config: ConfigService) {}

  onModuleInit(): void {
    const path = resolve(
      process.cwd(),
      this.config.get<string>('SCHEMA_PATH', '../../pricer/schema/thoth.schema.json'),
    );
    this.schema = JSON.parse(readFileSync(path, 'utf8'));
    this.ajv = new Ajv({ allErrors: true, strict: false });
    this.ajv.addSchema(this.schema, 'thoth');
    for (const [key, def] of Object.entries(this.schema.$defs)) {
      const title = def.title;
      if (typeof title === 'string' && title.startsWith('!')) {
        this.kindToDefKey.set(title.slice(1), key);
      }
    }
    this.log.log(`schema loaded from ${path}: ${this.kindToDefKey.size} kinds`);
  }

  kinds(): string[] {
    return [...this.kindToDefKey.keys()].sort();
  }

  //! the per-kind $defs map the front uses to build schema-driven forms (branch by kind)
  defs(): Record<string, KindDef> {
    const out: Record<string, KindDef> = {};
    for (const [kind, key] of this.kindToDefKey) out[kind] = this.schema.$defs[key];
    return out;
  }

  hasKind(kind: string): boolean {
    return this.kindToDefKey.has(kind);
  }

  //! Reference-bearing fields of a kind, derived from the $def: a property whose value
  //! is `{$ref:#/$defs/ref}` names one object; `#/$defs/refList` names several. Used by
  //! the semantic validator to check that references resolve (ajv cannot).
  refFields(kind: string): { single: string[]; list: string[] } {
    const key = this.kindToDefKey.get(kind);
    const single: string[] = [];
    const list: string[] = [];
    const def = key ? this.schema.$defs[key] : undefined;
    const props = (def?.properties ?? {}) as Record<string, { $ref?: string }>;
    for (const [field, spec] of Object.entries(props)) {
      if (spec.$ref === '#/$defs/ref') single.push(field);
      else if (spec.$ref === '#/$defs/refList') list.push(field);
    }
    return { single, list };
  }

  //! ajv-validate one object's payload against its kind definition. Returns the list of
  //! error messages ([] = valid). Note: ajv cannot check cross-references or array-length
  //! pairing — the semantic validator (common/) covers those.
  validateObject(kind: string, payload: Record<string, unknown>): string[] {
    const key = this.kindToDefKey.get(kind);
    if (!key) return [`unknown kind '${kind}'`];
    let validate = this.validators.get(kind);
    if (!validate) {
      validate = this.ajv.compile({ $ref: `thoth#/$defs/${key}` });
      this.validators.set(kind, validate);
    }
    return validate(payload) ? [] : (validate.errors ?? []).map((e) => `${e.instancePath || '/'} ${e.message}`);
  }
}
