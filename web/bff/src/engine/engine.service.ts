//! Thin Nest wrapper over @thoth/shared's EnginePool. The key responsibility beyond
//! pricing is PER-JOB PROGRESS: a grid job leases one replica for its whole duration,
//! and we remember jobId -> leased client so GET /grid/:jobId/progress can read that
//! replica's (process-global) /progress and have it mean THIS job.

import { Injectable, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { EngineClient, EnginePool, type ProgressSnapshot } from '@thoth/shared';

@Injectable()
export class EngineService {
  private readonly log = new Logger(EngineService.name);
  private readonly pool: EnginePool;
  private readonly activeByJob = new Map<string, EngineClient>();

  constructor(config: ConfigService) {
    const urls = config
      .get<string>('THOTH_ENGINE_URLS', 'http://localhost:8080')
      .split(',')
      .map((s) => s.trim())
      .filter(Boolean);
    const timeoutMs = Number(config.get<string>('ENGINE_TIMEOUT_MS', '120000'));
    this.pool = new EnginePool(urls, timeoutMs);
    this.log.log(`engine pool of ${this.pool.size} replica(s): ${urls.join(', ')}`);
  }

  get size(): number {
    return this.pool.size;
  }

  //! Price under a leased replica, tracked against jobId for the duration so its
  //! progress is queryable. Returns the result YAML plus which engine (server URL) ran
  //! it. Throws EngineError on an engine-reported failure.
  async priceForJob(
    jobId: string,
    bookYaml: string,
    taskName = 'root',
  ): Promise<{ yaml: string; server: string }> {
    const lease = await this.pool.acquire();
    this.activeByJob.set(jobId, lease.client);
    try {
      const yaml = await lease.client.postPrice(bookYaml, taskName);
      return { yaml, server: lease.client.baseUrl };
    } finally {
      this.activeByJob.delete(jobId);
      lease.release();
    }
  }

  //! Synchronous one-off pricing (no job tracking) — for non-grid uses and tests.
  async priceNow(bookYaml: string, taskName = 'root'): Promise<string> {
    return this.pool.withEngine((c) => c.postPrice(bookYaml, taskName));
  }

  //! Best-effort progress for a running job (null if the job isn't currently pricing).
  async jobProgress(jobId: string): Promise<ProgressSnapshot | null> {
    const client = this.activeByJob.get(jobId);
    return client ? client.progress() : null;
  }

  async healthyReplicas(): Promise<number> {
    return this.pool.healthy();
  }
}
