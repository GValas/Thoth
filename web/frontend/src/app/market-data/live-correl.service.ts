import { Injectable, inject, signal } from '@angular/core';
import { AuthService } from '../core/auth.service';

//! a full live correlation matrix over the streamed universe (equities + fx legs).
export interface CorrelSnapshot {
  members: string[];
  matrix: number[][];
  ts: number;
}

//! Live correlation matrix from the BFF (GET /api/spots/correl/latest snapshot, then the
//! /correl/stream SSE). Mirrors LiveSpotsService: fetch() with the Bearer token, reconnect on
//! drop. The matrix spans the whole universe; a view slices out its own members via value().
@Injectable({ providedIn: 'root' })
export class LiveCorrelService {
  private readonly auth = inject(AuthService);

  //! latest universe snapshot; a fresh object each update so signal consumers re-render.
  readonly snap = signal<CorrelSnapshot | null>(null);

  private index = new Map<string, number>();
  private ctrl?: AbortController;
  private retry?: ReturnType<typeof setTimeout>;
  private stopped = true;

  async start(): Promise<void> {
    if (!this.stopped) return;
    this.stopped = false;
    await this.snapshot();
    void this.stream();
  }

  stop(): void {
    this.stopped = true;
    this.ctrl?.abort();
    this.ctrl = undefined;
    if (this.retry) {
      clearTimeout(this.retry);
      this.retry = undefined;
    }
  }

  //! live correlation between two members, or undefined when off / either member is unknown.
  value(a: string, b: string): number | undefined {
    const s = this.snap();
    if (!s) return undefined;
    const ia = this.index.get(a);
    const ib = this.index.get(b);
    if (ia === undefined || ib === undefined) return undefined;
    return s.matrix[ia]?.[ib];
  }

  private authHeaders(extra: Record<string, string> = {}): Record<string, string> {
    const token = this.auth.token;
    return token ? { ...extra, Authorization: `Bearer ${token}` } : extra;
  }

  private apply(s: CorrelSnapshot | null): void {
    if (!s) return;
    this.index = new Map(s.members.map((m, i) => [m, i]));
    this.snap.set(s);
  }

  private async snapshot(): Promise<void> {
    try {
      const res = await fetch('/api/spots/correl/latest', { headers: this.authHeaders() });
      if (res.ok) this.apply((await res.json()) as CorrelSnapshot | null);
    } catch {
      /* best-effort; the stream will fill in */
    }
  }

  private async stream(): Promise<void> {
    this.ctrl = new AbortController();
    try {
      const res = await fetch('/api/spots/correl/stream', {
        headers: this.authHeaders({ Accept: 'text/event-stream' }),
        signal: this.ctrl.signal,
      });
      if (!res.ok || !res.body) throw new Error(`stream HTTP ${res.status}`);
      const reader = res.body.getReader();
      const decoder = new TextDecoder();
      let buf = '';
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buf += decoder.decode(value, { stream: true });
        let sep: number;
        while ((sep = buf.indexOf('\n\n')) >= 0) {
          const frame = buf.slice(0, sep);
          buf = buf.slice(sep + 2);
          const line = frame.split('\n').find((l) => l.startsWith('data:'));
          if (line) {
            try {
              this.apply(JSON.parse(line.slice(5).trim()) as CorrelSnapshot);
            } catch {
              /* skip a malformed frame */
            }
          }
        }
      }
    } catch {
      /* aborted (stop) or network error -> reconnect */
    }
    if (!this.stopped) {
      this.retry = setTimeout(() => void this.stream(), 2000);
    }
  }
}
