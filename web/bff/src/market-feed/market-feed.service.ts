//! Live spot feed bridge: one Redis subscription per BFF process consuming the spot-feed
//! service's ticks (PUBLISH spots.tick), re-emitted on an in-process RxJS subject that the
//! SSE controller fans out to browsers. The latest-snapshot hash (spots:latest) backs a
//! point-in-time read so a client renders immediately, then streams deltas.
//!
//! Resilient by design: if Redis is absent (e.g. the in-memory dev deployment) the
//! connections just keep retrying and the subject stays silent — no crash, the SSE stream
//! is simply empty.

import { Injectable, Logger, OnModuleDestroy, OnModuleInit } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { Redis } from 'ioredis';
import { Observable, Subject, filter } from 'rxjs';

const TICK_CHANNEL = 'spots.tick';
const LATEST_HASH = 'spots:latest';

export interface SpotTick {
  symbol: string;
  price: number;
  ts: number;
}

@Injectable()
export class MarketFeedService implements OnModuleInit, OnModuleDestroy {
  private readonly log = new Logger(MarketFeedService.name);
  private sub?: Redis;
  private client?: Redis;
  private readonly ticks$ = new Subject<SpotTick>();

  constructor(private readonly config: ConfigService) {}

  onModuleInit(): void {
    const host = this.config.get<string>('REDIS_HOST', 'localhost');
    const port = Number(this.config.get<string>('REDIS_PORT', '6379'));
    const opts = { host, port, maxRetriesPerRequest: null as null };

    this.client = new Redis(opts);
    this.sub = new Redis(opts);
    //! warn once-ish, don't spam: ioredis reconnects on its own
    this.client.on('error', (e: Error) => this.log.warn(`redis (client): ${e.message}`));
    this.sub.on('error', (e: Error) => this.log.warn(`redis (sub): ${e.message}`));

    this.sub.subscribe(TICK_CHANNEL).then(
      () => this.log.log(`subscribed to ${TICK_CHANNEL} on ${host}:${port}`),
      (e: Error) => this.log.warn(`subscribe failed: ${e.message}`),
    );
    this.sub.on('message', (_channel: string, message: string) => {
      try {
        this.ticks$.next(JSON.parse(message) as SpotTick);
      } catch {
        /* ignore a malformed tick */
      }
    });
  }

  onModuleDestroy(): void {
    this.ticks$.complete();
    void this.sub?.quit();
    void this.client?.quit();
  }

  //! live tick stream, optionally narrowed to a set of symbols.
  stream(symbols?: Set<string>): Observable<SpotTick> {
    return symbols && symbols.size
      ? this.ticks$.pipe(filter((t) => symbols.has(t.symbol)))
      : this.ticks$.asObservable();
  }

  //! point-in-time snapshot from the latest-spot hash (optionally filtered).
  async latest(symbols?: string[]): Promise<SpotTick[]> {
    if (!this.client) return [];
    let all: Record<string, string> = {};
    try {
      all = await this.client.hgetall(LATEST_HASH);
    } catch {
      return [];
    }
    const wanted = symbols && symbols.length ? new Set(symbols) : null;
    const out: SpotTick[] = [];
    for (const raw of Object.values(all)) {
      try {
        const t = JSON.parse(raw) as SpotTick;
        if (!wanted || wanted.has(t.symbol)) out.push(t);
      } catch {
        /* skip */
      }
    }
    return out;
  }
}
