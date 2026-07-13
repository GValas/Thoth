//! Build a ONE-contract book for a single hand-entered instrument (vanilla / barrier /
//! variance, …) and pivot its result block into a flat {premium, greeks} record.
//!
//! This is the single-product counterpart of grid-builder: instead of a strike x maturity
//! sweep it prices exactly one instrument so the GUI's pricing panels (and the monitoring
//! blotter) can quote an arbitrary product with all its variations. The engine-config /
//! correlation synthesis mirrors grid-builder so a panel priced straight from the GUI (no
//! configName) still references a valid !pde_configuration / !mcl_configuration.

import { TAG_KEY, sanitizeFields, type CellResult, type Engine } from './types.js';
import { dumpBook } from './yaml.js';

const PRICER_TAG: Record<Engine, string> = {
  ana: 'ana_pricer',
  pde: 'pde_pricer',
  mcl: 'mcl_pricer',
  mcl_gpu: 'mcl_pricer',
};

const GREEK_FIELDS = ['delta', 'gamma', 'vega', 'rho', 'theta'] as const;

//! Reserved names for the synthesised engine config and the single contract / book, kept
//! distinct from grid-builder's so the two never collide if ever merged into one document.
const DEFAULT_CONFIG_NAME = '_instrument_engine_config';
const DEFAULT_PDE_CONFIG: Record<string, unknown> = { vanilla_precision: 'medium' };
const DEFAULT_MCL_CONFIG: Record<string, unknown> = {
  max_day_step: 1,
  min_day_step: -1,
  paths: 50000,
  vol_year_step: 0.01,
  use_sobol: true,
};

export const CONTRACT_NAME = 'contract';
const BOOK_NAME = 'instrument_book';

//! A single hand-entered instrument: the engine kind tag (vanilla / barrier / variance)
//! plus the kind's own fields (underlying, strike, maturity, barrier_type, …). Fields are
//! passed through verbatim, so a panel can send any variation the engine accepts.
export interface InstrumentSpec {
  kind: string; //!< engine instrument kind, e.g. "vanilla" | "barrier" | "variance"
  fields: Record<string, unknown>; //!< the instrument's own fields (no kind tag)
}

//! A single-instrument pricing request: one engine, one reporting currency, the requested
//! indicators (premium + whichever Greeks) and the instrument itself.
export interface InstrumentRequest {
  engine: Engine;
  today: string; //!< YYYY-MM-DD
  currency: string; //!< reporting / pricer currency (must exist among the objects)
  indicators: string[]; //!< e.g. ["premium","delta","gamma","vega","rho","theta"]
  instrument: InstrumentSpec;
}

export interface InstrumentContext {
  pricerName: string; //!< name of the pricer object (its result block = the `result:` value)
  resultName: string; //!< the pricer's `result:` value (where the contract's fields land)
  configName?: string; //!< pde_configuration / mcl_configuration object name (required for pde/mcl)
  correlationName?: string; //!< optional correlation object
  //! supporting objects already stored for the workspace (underlyings, vols, curves,
  //! currency, engine config, correlation), as tagged {name,kind,payload}.
  supportObjects: ReadonlyArray<{ name: string; kind: string; payload: Record<string, unknown> }>;
}

//! Build the full tagged document (root + pricer + book + the single contract + support
//! objects) ready to dump to YAML. Returned as a plain object tree so it is unit-testable.
export function buildInstrumentDoc(
  req: InstrumentRequest,
  ctx: InstrumentContext,
): Record<string, unknown> {
  const doc: Record<string, unknown> = { root: ctx.pricerName };

  // the single contract, tagged by its kind, fields passed through verbatim. The tag is
  // written AFTER the (sanitized) fields so a crafted `__tag` in the fields can never
  // override the validated kind — the kind always wins.
  doc[CONTRACT_NAME] = { ...sanitizeFields(req.instrument.fields), [TAG_KEY]: req.instrument.kind };

  const pricer: Record<string, unknown> = {
    [TAG_KEY]: PRICER_TAG[req.engine],
    today: req.today,
    book: BOOK_NAME,
    currency: req.currency,
    indicators: req.indicators,
    result: ctx.resultName,
  };

  // Correlation: explicit ref wins; otherwise auto-attach the workspace's correlation
  // matrix when there is one (MCL diffusion mandates it; harmless elsewhere).
  const correlationName =
    ctx.correlationName ?? ctx.supportObjects.find((o) => o.kind === 'correlation_matrix')?.name;
  if (correlationName) pricer.correlation = correlationName;

  // Engine config: explicit ref wins; otherwise synthesise a default object so a product
  // priced from the GUI (no configName) still has a valid pde/mcl_configuration reference.
  if (req.engine !== 'ana') {
    const field = req.engine === 'pde' ? 'pde_configuration' : 'mcl_configuration';
    const configName = ctx.configName ?? DEFAULT_CONFIG_NAME;
    pricer[field] = configName;
    if (!ctx.configName) {
      const defaults =
        req.engine === 'pde'
          ? DEFAULT_PDE_CONFIG
          : req.engine === 'mcl_gpu'
            ? { ...DEFAULT_MCL_CONFIG, allow_gpu: true }
            : DEFAULT_MCL_CONFIG;
      doc[configName] = { [TAG_KEY]: field, ...defaults };
    }
  }
  doc[ctx.pricerName] = pricer;

  doc[BOOK_NAME] = { [TAG_KEY]: 'book', contracts: [CONTRACT_NAME] };

  // merge the workspace's supporting market objects (a user object with the same name as a
  // synthesised default deliberately wins — the user's config overrides ours). Payload is
  // user-supplied, so sanitize it and keep the stored kind as the tag (tag after spread).
  for (const o of ctx.supportObjects) {
    doc[o.name] = { ...sanitizeFields(o.payload), [TAG_KEY]: o.kind };
  }
  return doc;
}

//! A termsheet request: the same hand-entered instrument, documented instead of priced
//! (the engine's !termsheet task renders its booked description as Markdown).
export interface TermsheetRequest {
  today: string; //!< as-of date the relative levels resolve against (YYYY-MM-DD)
  instrument: InstrumentSpec;
  title?: string; //!< optional document title
  issuer?: string; //!< optional issuer header line
}

export const TERMSHEET_TASK_NAME = 'termsheet_task';
export const TERMSHEET_RESULT_NAME = 'termsheet_result';

//! Build the tagged document for a !termsheet task over the single contract: no pricer,
//! no book, no engine config — just the task, the contract and the workspace's supporting
//! market objects (the underlying/vol/curve/currency the contract references).
export function buildTermsheetDoc(
  req: TermsheetRequest,
  supportObjects: InstrumentContext['supportObjects'],
): Record<string, unknown> {
  const doc: Record<string, unknown> = { root: TERMSHEET_TASK_NAME };
  // tag after the (sanitized) fields so the validated kind always wins (see buildInstrumentDoc).
  doc[CONTRACT_NAME] = { ...sanitizeFields(req.instrument.fields), [TAG_KEY]: req.instrument.kind };
  const task: Record<string, unknown> = {
    [TAG_KEY]: 'termsheet',
    today: req.today,
    contract: CONTRACT_NAME,
    result: TERMSHEET_RESULT_NAME,
  };
  if (req.title) task.title = req.title;
  if (req.issuer) task.issuer = req.issuer;
  doc[TERMSHEET_TASK_NAME] = task;
  for (const o of supportObjects) {
    doc[o.name] = { ...sanitizeFields(o.payload), [TAG_KEY]: o.kind };
  }
  return doc;
}

//! Serialize a single-instrument request to the YAML book the engine prices.
export function buildInstrumentYaml(
  req: InstrumentRequest,
  ctx: InstrumentContext,
  kinds: readonly string[],
): string {
  return dumpBook(buildInstrumentDoc(req, ctx), kinds);
}

//! One instrument's flat result: premium plus whichever requested Greeks the engine wrote.
export interface InstrumentResult {
  premium: number;
  greeks: Partial<Record<keyof CellResult, number>>;
}

//! Pivot a result block (keys like `contract_premium`, `contract_delta`) into a flat record.
//! Missing/non-finite fields are simply omitted (variance, for instance, may write no
//! Greeks); premium falls back to NaN so the caller can show "—".
export function parseInstrumentResult(
  resultBlock: Record<string, unknown>,
  indicators: string[],
): InstrumentResult {
  const greeks: Partial<Record<keyof CellResult, number>> = {};
  for (const g of GREEK_FIELDS) {
    if (!indicators.includes(g)) continue;
    const v = num(resultBlock[`${CONTRACT_NAME}_${g}`]);
    if (Number.isFinite(v)) greeks[g] = v;
  }
  return { premium: num(resultBlock[`${CONTRACT_NAME}_premium`]), greeks };
}

function num(v: unknown): number {
  const n = typeof v === 'number' ? v : Number(v);
  return Number.isFinite(n) ? n : NaN;
}
