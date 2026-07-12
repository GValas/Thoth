//! The pricing-grid pipeline: build one book of N vanillas from the workspace's market
//! data + the chosen engine, enqueue it, and (in the processor) price it on a leased
//! replica and pivot the per-cell results into matrices. State lives in GridRun so the
//! UI can poll status/progress and results survive a BFF restart (BullMQ driver).

import {
  BadRequestException,
  Injectable,
  NotFoundException,
  OnModuleInit,
  OnModuleDestroy,
  Logger,
} from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import {
  buildGridDoc,
  dumpBook,
  loadBook,
  parseGridResult,
  EngineError,
  type GridContext,
  type GridMatrix,
  type GridRequest,
} from '@thoth/shared';
import { GridRun } from '../persistence/entities';
import type { AuthUser } from '../common/decorators';
import { overlayCorrelation, overlaySpots } from '../common/live-overlay';
import { EngineService } from '../engine/engine.service';
import { SchemaService } from '../schema/schema.service';
import { WorkspacesService } from '../workspaces/workspaces.service';
import { MarketDataService } from '../marketdata/marketdata.service';
import { MarketFeedService } from '../market-feed/market-feed.service';
import { MemoryGridQueue, createBullMqQueue, type GridQueue } from './queue';

//! the BFF-side request: pricing axes + which workspace + optional engine config refs.
export interface GridSubmitDto extends Omit<GridRequest, 'today' | 'currency'> {
  workspaceId: string;
  configName?: string;
  correlationName?: string;
  today?: string;
  currency?: string;
}

@Injectable()
export class GridService implements OnModuleInit, OnModuleDestroy {
  private readonly log = new Logger(GridService.name);
  private queue!: GridQueue;

  constructor(
    @InjectRepository(GridRun) private readonly runs: Repository<GridRun>,
    private readonly config: ConfigService,
    private readonly engine: EngineService,
    private readonly schema: SchemaService,
    private readonly workspaces: WorkspacesService,
    private readonly marketData: MarketDataService,
    private readonly feed: MarketFeedService,
  ) {}

  async onModuleInit(): Promise<void> {
    const driver = this.config.get<string>('QUEUE_DRIVER', 'memory');
    const processor = (jobId: string) => this.process(jobId);
    if (driver === 'bullmq') {
      const host = this.config.get<string>('REDIS_HOST', 'localhost');
      const port = Number(this.config.get<string>('REDIS_PORT', '6379'));
      this.queue = await createBullMqQueue({ host, port }, this.engine.size, processor);
      this.log.log(`grid queue: bullmq (${host}:${port}), concurrency ${this.engine.size}`);
    } else {
      this.queue = new MemoryGridQueue(processor);
      this.log.log('grid queue: in-memory (concurrency gated by the engine pool)');
    }
  }

  async onModuleDestroy(): Promise<void> {
    await this.queue?.close();
  }

  //! Total cell count a single grid may fan out to. Beyond this a request is rejected up
  //! front, so no unbounded pricing job (memory + engine time) can be enqueued from one call.
  private static readonly MAX_GRID_CELLS = 50_000;

  //! Reject a grid whose fan-out (underlyings × types × strikes × maturities) exceeds the cap.
  private assertGridSize(dto: GridSubmitDto): void {
    const cells =
      dto.underlyings.length * dto.types.length * dto.strikes.length * dto.maturities.length;
    if (cells > GridService.MAX_GRID_CELLS) {
      throw new BadRequestException(
        `grid too large: ${cells} cells exceeds the ${GridService.MAX_GRID_CELLS} limit`,
      );
    }
  }

  //! Persist a queued GridRun and submit it; returns the job id to poll. Authorizes the
  //! workspace for the caller (404 if not theirs) and bounds the grid size before enqueuing.
  async submit(dto: GridSubmitDto, user: AuthUser): Promise<{ jobId: string }> {
    const isAdmin = user.role === 'admin';
    await this.workspaces.get(dto.workspaceId, user.userId, isAdmin);
    this.assertGridSize(dto);
    const run = await this.runs.save(
      this.runs.create({
        workspaceId: dto.workspaceId,
        userId: user.userId,
        status: 'queued',
        request: { ...dto },
      }),
    );
    await this.queue.submit(run.id);
    return { jobId: run.id };
  }

  //! Live re-pricing: price the grid SYNCHRONOUSLY (no queue, no GridRun) with the latest
  //! live spots AND the live correlation matrix overlaid onto the workspace objects,
  //! returning the matrices directly. The frontend's Live mode calls this on a throttle.
  //! Equities the feed does not quote keep their stored spot (a partial feed still prices),
  //! and the correlation blend falls back to the stored matrix if mixing live and stored
  //! pairs would break positive-definiteness (see overlayCorrelation).
  async priceLive(
    dto: GridSubmitDto,
    user: AuthUser,
  ): Promise<{
    matrices: GridMatrix[];
    meta: { execMs: number; engineMs?: number; engineVersion?: string };
  }> {
    const t0 = Date.now();
    //! authorize the workspace for the caller (404 if not theirs) and bound the fan-out.
    const ws = await this.workspaces.get(dto.workspaceId, user.userId, user.role === 'admin');
    this.assertGridSize(dto);
    const req: GridRequest = {
      engine: dto.engine,
      today: dto.today ?? ws.today,
      currency: dto.currency ?? ws.currency,
      underlyings: dto.underlyings,
      types: dto.types,
      strikes: dto.strikes,
      maturities: dto.maturities,
      indicators: dto.indicators,
      exercise: dto.exercise,
    };

    const support = await this.marketData.listObjects(dto.workspaceId);
    const [live, correl] = await Promise.all([this.feed.latest(), this.feed.latestCorrel()]);
    const supportObjects = overlayCorrelation(overlaySpots(support, live), correl);

    const ctx: GridContext = {
      pricerName: 'grid',
      resultName: 'grid_result',
      configName: dto.configName,
      correlationName: dto.correlationName,
      supportObjects,
    };
    const bookYaml = dumpBook(buildGridDoc(req, ctx), this.schema.kinds());
    const resultYaml = await this.engine.priceNow(bookYaml, 'grid');
    const resultDoc = loadBook(resultYaml, this.schema.kinds()) as Record<string, unknown>;
    const block = (resultDoc['grid_result'] ?? {}) as Record<string, unknown>;
    const matrices = parseGridResult(block, req);

    const sys = (resultDoc['system_information'] ?? {}) as Record<string, unknown>;
    const engineSec =
      typeof block['task_time'] === 'number'
        ? (block['task_time'] as number)
        : typeof sys['task_time'] === 'number'
          ? (sys['task_time'] as number)
          : undefined;
    const meta = {
      execMs: Date.now() - t0,
      engineMs: engineSec !== undefined ? Math.round(engineSec * 1000) : undefined,
      engineVersion: typeof sys['version'] === 'string' ? (sys['version'] as string) : undefined,
    };
    return { matrices, meta };
  }

  //! Fetch a run and authorize the caller. A run belongs to the user who submitted it (its
  //! stored userId) AND lives under a workspace; we require the run's userId to match (admins
  //! bypass). A run of another user is reported as 404 to avoid job-id enumeration.
  async get(jobId: string, user: AuthUser): Promise<GridRun> {
    const run = await this.runs.findOne({ where: { id: jobId } });
    if (!run) throw new NotFoundException('grid run not found');
    if (user.role !== 'admin' && run.userId !== user.userId) {
      throw new NotFoundException('grid run not found');
    }
    return run;
  }

  //! Live progress for a running job (best-effort; null unless it is currently pricing).
  async progress(jobId: string, user: AuthUser) {
    const run = await this.get(jobId, user);
    const live = await this.engine.jobProgress(jobId);
    return { status: run.status, progress: live };
  }

  //! Worker body: materialise the book, price on a leased replica, pivot, persist.
  private async process(jobId: string): Promise<void> {
    const run = await this.runs.findOne({ where: { id: jobId } });
    if (!run) return;
    const dto = run.request as unknown as GridSubmitDto;
    const t0 = Date.now();
    try {
      await this.runs.update(jobId, { status: 'running' });
      //! Internal worker: ownership was already enforced at submit() time, so resolve the
      //! workspace with the admin bypass (isAdmin=true) rather than re-deriving a principal.
      const ws = await this.workspaces.get(dto.workspaceId, run.userId ?? '', true);
      const req: GridRequest = {
        engine: dto.engine,
        today: dto.today ?? ws.today,
        currency: dto.currency ?? ws.currency,
        underlyings: dto.underlyings,
        types: dto.types,
        strikes: dto.strikes,
        maturities: dto.maturities,
        indicators: dto.indicators,
        exercise: dto.exercise,
      };
      const ctx: GridContext = {
        pricerName: 'grid',
        resultName: 'grid_result',
        configName: dto.configName,
        correlationName: dto.correlationName,
        supportObjects: await this.marketData.listObjects(dto.workspaceId),
      };
      const bookYaml = dumpBook(buildGridDoc(req, ctx), this.schema.kinds());

      const { yaml: resultYaml, server } = await this.engine.priceForJob(jobId, bookYaml, 'grid');
      const resultDoc = loadBook(resultYaml, this.schema.kinds()) as Record<string, unknown>;
      const block = (resultDoc['grid_result'] ?? {}) as Record<string, unknown>;
      const matrices = parseGridResult(block, req);

      // provenance: which server ran it, round-trip wall clock, the engine's own compute
      // time (task_time, seconds) and build version — surfaced to the UI as a note.
      const sys = (resultDoc['system_information'] ?? {}) as Record<string, unknown>;
      const engineSec =
        typeof block['task_time'] === 'number'
          ? (block['task_time'] as number)
          : typeof sys['task_time'] === 'number'
            ? (sys['task_time'] as number)
            : undefined;
      const meta = {
        server,
        execMs: Date.now() - t0,
        engineMs: engineSec !== undefined ? Math.round(engineSec * 1000) : undefined,
        engineVersion: typeof sys['version'] === 'string' ? (sys['version'] as string) : undefined,
      };

      await this.runs.update(jobId, {
        status: 'done',
        result: { matrices, meta },
        execMs: meta.execMs,
      });
    } catch (e) {
      const msg = e instanceof EngineError ? e.message : e instanceof Error ? e.message : String(e);
      await this.runs.update(jobId, { status: 'error', error: msg, execMs: Date.now() - t0 });
      this.log.warn(`grid job ${jobId} errored: ${msg}`);
    }
  }
}
