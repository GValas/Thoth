//! Build a strike x maturity vanilla price grid as ONE book of N contracts priced in a
//! single /price request (the efficient design — one diffusion sweep for MCL), then
//! pivot the per-cell results into matrices.
//!
//! Per-cell premium/premium_trust are written by every engine; per-cell Greeks only by
//! ANA/PDE (and GPU-MCL) — see pricer.cpp WriteResults gating. The caller picks the
//! engine (no default); for MCL the Greeks matrices come back empty.

import { TAG_KEY, type CellResult, type Engine, type GridMatrix, type GridRequest } from './types.js';
import { dumpBook } from './yaml.js';

const PRICER_TAG: Record<Engine, string> = {
  ana: 'ana_pricer',
  pde: 'pde_pricer',
  mcl: 'mcl_pricer',
};

const GREEK_FIELDS = ['delta', 'gamma', 'vega', 'rho', 'theta'] as const;

//! Reserved name for the engine config synthesised when the caller provides none, so a
//! pde/mcl grid priced straight from the GUI (which sends no configName) still references
//! a valid !pde_configuration / !mcl_configuration object instead of emitting a null ref
//! (which the engine rejects with "pde_configuration must be a string").
const DEFAULT_CONFIG_NAME = '_grid_engine_config';

//! Default parameter objects, mirroring the canonical single-vanilla samples
//! (samples/simple_call.out.yaml for MCL; the engine's own "high" preset for PDE).
const DEFAULT_PDE_CONFIG: Record<string, unknown> = { vanilla_precision: 'high' };
const DEFAULT_MCL_CONFIG: Record<string, unknown> = {
  max_day_step: 1,
  min_day_step: -1,
  paths: 50000,
  vol_year_step: 0.01,
  use_sobol: true,
};

//! Engines that emit per-contract Greek fields (GreeksPerContract()==true).
export function engineHasPerCellGreeks(engine: Engine): boolean {
  return engine === 'ana' || engine === 'pde';
}

function pad(n: number, width: number): string {
  return String(n).padStart(width, '0');
}

//! Deterministic, set-order-stable cell name: cell__<underlying>__<type>__<i>__<j>.
export function cellName(
  underlying: string,
  type: string,
  i: number,
  j: number,
  iWidth: number,
  jWidth: number,
): string {
  return `cell__${underlying}__${type}__${pad(i, iWidth)}__${pad(j, jWidth)}`;
}

export interface GridContext {
  pricerName: string; //!< name of the pricer object (its result block = `${pricerName}_result`)
  resultName: string; //!< the pricer's `result:` value (where per-cell fields land)
  configName?: string; //!< pde_configuration / mcl_configuration object name (required for pde/mcl)
  correlationName?: string; //!< optional correlation object
  //! supporting objects already stored for the workspace (underlyings, vols, curves,
  //! currency, engine config, correlation), as tagged {name,kind,payload}.
  supportObjects: ReadonlyArray<{ name: string; kind: string; payload: Record<string, unknown> }>;
}

//! The full tagged document (root + pricer + book + cells + support objects) ready to
//! dump to YAML. Returned as a plain object tree so it is unit-testable without YAML.
export function buildGridDoc(req: GridRequest, ctx: GridContext): Record<string, unknown> {
  const iWidth = String(req.strikes.length - 1).length;
  const jWidth = String(req.maturities.length - 1).length;
  const exercise = req.exercise ?? 'european';

  const cells: string[] = [];
  const doc: Record<string, unknown> = { root: ctx.pricerName };

  // one !vanilla per (underlying, type, strike, maturity)
  for (const u of req.underlyings) {
    for (const type of req.types) {
      for (let i = 0; i < req.strikes.length; i++) {
        for (let j = 0; j < req.maturities.length; j++) {
          const name = cellName(u, type, i, j, iWidth, jWidth);
          cells.push(name);
          doc[name] = {
            [TAG_KEY]: 'vanilla',
            underlying: u,
            premium_currency: req.currency,
            strike: req.strikes[i],
            maturity: req.maturities[j],
            type,
            exercise,
          };
        }
      }
    }
  }

  // the pricer for the chosen engine
  const pricer: Record<string, unknown> = {
    [TAG_KEY]: PRICER_TAG[req.engine],
    today: req.today,
    book: 'grid_book',
    currency: req.currency,
    indicators: req.indicators,
    result: ctx.resultName,
  };
  // Correlation: explicit ref wins; otherwise auto-attach the workspace's correlation
  // matrix when there is one (MCL diffusion mandates it; harmless/quanto-useful elsewhere).
  const correlationName =
    ctx.correlationName ?? ctx.supportObjects.find((o) => o.kind === 'correlation_matrix')?.name;
  if (correlationName) pricer.correlation = correlationName;

  // Engine config: explicit ref wins; otherwise synthesise a default object so a grid
  // priced from the GUI (no configName) still has a valid pde/mcl_configuration reference.
  if (req.engine === 'pde' || req.engine === 'mcl') {
    const field = req.engine === 'pde' ? 'pde_configuration' : 'mcl_configuration';
    const configName = ctx.configName ?? DEFAULT_CONFIG_NAME;
    pricer[field] = configName;
    if (!ctx.configName) {
      doc[configName] = {
        [TAG_KEY]: field,
        ...(req.engine === 'pde' ? DEFAULT_PDE_CONFIG : DEFAULT_MCL_CONFIG),
      };
    }
  }
  doc[ctx.pricerName] = pricer;

  doc.grid_book = { [TAG_KEY]: 'book', contracts: cells };

  // merge the workspace's supporting market objects (a workspace object with the same
  // name as a synthesised default deliberately wins — the user's config overrides ours).
  for (const o of ctx.supportObjects) {
    doc[o.name] = { [TAG_KEY]: o.kind, ...o.payload };
  }
  return doc;
}

//! Serialize a grid request to the YAML book the engine prices.
export function buildGridYaml(req: GridRequest, ctx: GridContext, kinds: readonly string[]): string {
  return dumpBook(buildGridDoc(req, ctx), kinds);
}

//! Pivot a result block (the parsed `${resultName}` object, with keys like
//! `cell__eq__call__0__1_premium`) into one matrix per (underlying, type).
export function parseGridResult(
  resultBlock: Record<string, unknown>,
  req: GridRequest,
): GridMatrix[] {
  const iWidth = String(req.strikes.length - 1).length;
  const jWidth = String(req.maturities.length - 1).length;
  const wantGreeks = engineHasPerCellGreeks(req.engine);
  const matrices: GridMatrix[] = [];

  for (const u of req.underlyings) {
    for (const type of req.types) {
      const premium = makeMatrix(req.strikes.length, req.maturities.length);
      const greeks: Partial<Record<keyof CellResult, number[][]>> = {};
      if (wantGreeks) {
        for (const g of GREEK_FIELDS) {
          if (req.indicators.includes(g)) greeks[g] = makeMatrix(req.strikes.length, req.maturities.length);
        }
      }
      for (let i = 0; i < req.strikes.length; i++) {
        for (let j = 0; j < req.maturities.length; j++) {
          const base = cellName(u, type, i, j, iWidth, jWidth);
          premium[i][j] = num(resultBlock[`${base}_premium`]);
          for (const g of GREEK_FIELDS) {
            const m = greeks[g];
            if (m) m[i][j] = num(resultBlock[`${base}_${g}`]);
          }
        }
      }
      matrices.push({
        underlying: u,
        type,
        currency: req.currency,
        strikes: req.strikes,
        maturities: req.maturities,
        premium,
        greeks,
      });
    }
  }
  return matrices;
}

function makeMatrix(rows: number, cols: number): number[][] {
  return Array.from({ length: rows }, () => new Array<number>(cols).fill(NaN));
}

function num(v: unknown): number {
  const n = typeof v === 'number' ? v : Number(v);
  return Number.isFinite(n) ? n : NaN;
}
