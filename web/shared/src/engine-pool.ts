//! A pool of `thoth -server` replicas. The engine prices one book at a time (global
//! mutex) and exposes a process-global GET /progress with no per-request id. By leasing
//! exactly ONE replica per job for the job's duration, that replica's /progress IS that
//! job's progress — which is how the BFF correlates progress to a specific grid run.
//!
//! The pool is a fair semaphore over N clients: at most N jobs price concurrently (one
//! per replica), further jobs queue. Pair with a BFF job queue (BullMQ) whose worker
//! concurrency == pool size.

import { EngineClient } from './engine-client.js';

export interface LeasedEngine {
  readonly client: EngineClient;
  release(): void;
}

export class EnginePool {
  private readonly free: EngineClient[];
  private readonly waiters: Array<(c: EngineClient) => void> = [];
  readonly size: number;

  constructor(baseUrls: string[], timeoutMs?: number) {
    if (baseUrls.length === 0) {
      throw new Error('EnginePool needs at least one engine URL');
    }
    this.free = baseUrls.map((u) => new EngineClient({ baseUrl: u, timeoutMs }));
    this.size = this.free.length;
  }

  //! Lease a replica, waiting if all are busy. Always call release() (use acquire()'s
  //! returned handle in a try/finally) so the replica returns to the pool.
  async acquire(): Promise<LeasedEngine> {
    const client = await new Promise<EngineClient>((resolve) => {
      const c = this.free.pop();
      if (c) {
        resolve(c);
      } else {
        this.waiters.push(resolve);
      }
    });
    let released = false;
    return {
      client,
      release: () => {
        if (released) return;
        released = true;
        const next = this.waiters.shift();
        if (next) {
          next(client); //!< hand the just-freed replica straight to a waiter
        } else {
          this.free.push(client);
        }
      },
    };
  }

  //! Run fn with a leased replica, releasing it even on throw. Convenience wrapper.
  async withEngine<T>(fn: (client: EngineClient) => Promise<T>): Promise<T> {
    const lease = await this.acquire();
    try {
      return await fn(lease.client);
    } finally {
      lease.release();
    }
  }

  //! How many replicas are reachable right now (for /health and readiness).
  async healthy(): Promise<number> {
    const results = await Promise.all(this.free.map((c) => c.health()));
    // note: only currently-free clients are probed; busy ones are presumed alive.
    return results.filter(Boolean).length + (this.size - this.free.length);
  }
}
