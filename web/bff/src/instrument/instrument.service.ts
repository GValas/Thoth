//! Single-instrument pricing pipeline: build a one-contract book from a hand-entered
//! product (vanilla / barrier / variance_swap) + the workspace's market data, price it
//! SYNCHRONOUSLY on a leased replica, and pivot the result into {premium, greeks}. This is
//! the single-product counterpart of GridService — used by the GUI's pricing panels and the
//! monitoring blotter. With `live`, the latest live spots AND the live correlation matrix
//! are overlaid onto the workspace objects (exactly like GridService.priceLive) so a
//! panel/blotter quotes off the same live market the Market Data screen shows.

import { Injectable, Logger } from '@nestjs/common';
import {
  buildInstrumentDoc,
  buildTermsheetDoc,
  dumpBook,
  loadBook,
  parseInstrumentResult,
  EngineError,
  TERMSHEET_RESULT_NAME,
  TERMSHEET_TASK_NAME,
  type Engine,
  type InstrumentContext,
  type InstrumentRequest,
  type InstrumentResult,
  type TermsheetRequest,
} from '@thoth/shared';
import type { AuthUser } from '../common/decorators';
import { overlayCorrelation, overlaySpots } from '../common/live-overlay';
import { EngineService } from '../engine/engine.service';
import { SchemaService } from '../schema/schema.service';
import { WorkspacesService } from '../workspaces/workspaces.service';
import { MarketDataService } from '../marketdata/marketdata.service';
import { MarketFeedService } from '../market-feed/market-feed.service';

//! the BFF-side request: which workspace, engine, instrument kind + fields, indicators and
//! (optionally) live-spot overlay + explicit engine-config / correlation refs.
export interface InstrumentPriceDto {
  workspaceId: string;
  engine: Engine;
  kind: string; //!< engine instrument kind, e.g. "vanilla" | "barrier" | "variance_swap"
  instrument: Record<string, unknown>; //!< the instrument's own fields (no kind tag)
  indicators: string[];
  currency?: string;
  today?: string;
  live?: boolean;
  configName?: string;
  correlationName?: string;
}

export interface InstrumentPriceResponse {
  result: InstrumentResult;
  currency: string;
  meta: { server?: string; execMs: number; engineMs?: number; engineVersion?: string };
}

//! a termsheet request: the same hand-entered instrument, documented instead of priced
//! (no engine, no indicators — the !termsheet task renders the description as Markdown).
export interface InstrumentTermsheetDto {
  workspaceId: string;
  kind: string;
  instrument: Record<string, unknown>;
  today?: string;
  title?: string;
  issuer?: string;
}

export interface InstrumentTermsheetResponse {
  termsheet: string; //!< the rendered Markdown document
  filename: string; //!< a suggested download filename
}

@Injectable()
export class InstrumentService {
  private readonly log = new Logger(InstrumentService.name);

  constructor(
    private readonly engine: EngineService,
    private readonly schema: SchemaService,
    private readonly workspaces: WorkspacesService,
    private readonly marketData: MarketDataService,
    private readonly feed: MarketFeedService,
  ) {}

  //! Price one instrument and return its premium + Greeks. Throws EngineError on an
  //! engine-reported failure (surfaced to the client as a 4xx by the controller).
  async price(dto: InstrumentPriceDto, user: AuthUser): Promise<InstrumentPriceResponse> {
    const t0 = Date.now();
    //! authorize the workspace for the caller (404 if not theirs) before pricing off its data.
    const ws = await this.workspaces.get(dto.workspaceId, user.userId, user.role === 'admin');
    const currency = dto.currency ?? ws.currency;

    const req: InstrumentRequest = {
      engine: dto.engine,
      today: dto.today ?? ws.today,
      currency,
      indicators: dto.indicators,
      instrument: { kind: dto.kind, fields: dto.instrument },
    };

    // support objects; when live, overlay the latest spots onto quoted equities and the
    // live correlation matrix onto the workspace correlation (PD-gated, stored fallback).
    let supportObjects = await this.marketData.listObjects(dto.workspaceId);
    if (dto.live) {
      const [live, correl] = await Promise.all([this.feed.latest(), this.feed.latestCorrel()]);
      supportObjects = overlayCorrelation(overlaySpots(supportObjects, live), correl);
    }

    const ctx: InstrumentContext = {
      pricerName: 'pricer',
      resultName: 'pricer_result',
      configName: dto.configName,
      correlationName: dto.correlationName,
      supportObjects,
    };

    const bookYaml = dumpBook(buildInstrumentDoc(req, ctx), this.schema.kinds());
    const { yaml: resultYaml, server } = await this.engine.priceNowWithServer(bookYaml, 'pricer');
    const resultDoc = loadBook(resultYaml, this.schema.kinds()) as Record<string, unknown>;
    const block = (resultDoc['pricer_result'] ?? {}) as Record<string, unknown>;
    const result = parseInstrumentResult(block, dto.indicators);

    const sys = (resultDoc['system_information'] ?? {}) as Record<string, unknown>;
    const engineSec =
      typeof block['task_time'] === 'number'
        ? (block['task_time'] as number)
        : typeof sys['task_time'] === 'number'
          ? (sys['task_time'] as number)
          : undefined;

    return {
      result,
      currency,
      meta: {
        server,
        execMs: Date.now() - t0,
        engineMs: engineSec !== undefined ? Math.round(engineSec * 1000) : undefined,
        engineVersion: typeof sys['version'] === 'string' ? (sys['version'] as string) : undefined,
      },
    };
  }

  //! Render one instrument's termsheet: build a !termsheet-task document over the same
  //! contract + workspace market objects, run it on the engine and return the Markdown.
  async termsheet(
    dto: InstrumentTermsheetDto,
    user: AuthUser,
  ): Promise<InstrumentTermsheetResponse> {
    //! authorize the workspace for the caller (404 if not theirs) before reading its data.
    const ws = await this.workspaces.get(dto.workspaceId, user.userId, user.role === 'admin');
    const today = dto.today ?? ws.today;
    const req: TermsheetRequest = {
      today,
      instrument: { kind: dto.kind, fields: dto.instrument },
      title: dto.title,
      issuer: dto.issuer,
    };
    const supportObjects = await this.marketData.listObjects(dto.workspaceId);

    const bookYaml = dumpBook(buildTermsheetDoc(req, supportObjects), this.schema.kinds());
    const { yaml: resultYaml } = await this.engine.priceNowWithServer(
      bookYaml,
      TERMSHEET_TASK_NAME,
    );
    const resultDoc = loadBook(resultYaml, this.schema.kinds()) as Record<string, unknown>;
    const block = (resultDoc[TERMSHEET_RESULT_NAME] ?? {}) as Record<string, unknown>;
    const termsheet = block['termsheet'];
    if (typeof termsheet !== 'string' || termsheet.length === 0) {
      throw new EngineError('the engine returned no termsheet document');
    }
    this.log.log(`termsheet rendered for kind=${dto.kind} (${termsheet.length} chars)`);
    return { termsheet, filename: `termsheet_${dto.kind}_${today}.md` };
  }
}
