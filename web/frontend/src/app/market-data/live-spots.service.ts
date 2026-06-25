import { Injectable, inject, signal } from '@angular/core';
import { AuthService } from '../core/auth.service';

export interface SpotTick {
  symbol: string;
  price: number;
  ts: number;
}

//! one symbol's live view: latest price + the previous one (for up/down colouring).
export interface SpotView {
  symbol: string;
  price: number;
  prev: number;
  ts: number;
}

//! Live equity spots from the BFF (GET /api/spots/latest snapshot, then the /stream SSE
//! deltas). Uses fetch() rather than EventSource so it can send the Bearer access token
//! (EventSource can't set headers); on stream end/error it reconnects after a short delay.
@Injectable({ providedIn: 'root' })
export class LiveSpotsService {
  private readonly auth = inject(AuthService);

  //! symbol -> latest view; a fresh Map each update so signal consumers re-render.
  readonly spots = signal<Map<string, SpotView>>(new Map());

  //! user-facing on/off for live updates (the "Live" toggle). The page wires an effect:
  //! enabled -> start(symbols), disabled -> stop(). Frozen ticks stay in `spots` so the
  //! UI can choose to keep showing the last value or fall back to stored data.
  readonly enabled = signal(true);

  private ctrl?: AbortController;
  private retry?: ReturnType<typeof setTimeout>;
  private symbols: string[] = [];
  private stopped = true;

  //! (re)start the feed for a set of symbols. A no-op when already streaming the same set,
  //! so unrelated market-data edits don't tear the connection down and up.
  async start(symbols: string[]): Promise<void> {
    const same =
      !this.stopped && symbols.length === this.symbols.length && symbols.every((s, i) => s === this.symbols[i]);
    if (same) return;
    this.stop();
    this.stopped = false;
    this.symbols = [...symbols];
    if (!this.symbols.length) return;
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

  private url(path: string): string {
    const q = this.symbols.length ? `?symbols=${encodeURIComponent(this.symbols.join(','))}` : '';
    return `/api/spots/${path}${q}`;
  }

  private authHeaders(extra: Record<string, string> = {}): Record<string, string> {
    const token = this.auth.token;
    return token ? { ...extra, Authorization: `Bearer ${token}` } : extra;
  }

  private async snapshot(): Promise<void> {
    try {
      const res = await fetch(this.url('latest'), { headers: this.authHeaders() });
      if (res.ok) this.apply((await res.json()) as SpotTick[]);
    } catch {
      /* snapshot is best-effort; the stream will fill in */
    }
  }

  private async stream(): Promise<void> {
    this.ctrl = new AbortController();
    try {
      const res = await fetch(this.url('stream'), {
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
        //! SSE frames are separated by a blank line; each carries one "data:" JSON payload.
        let sep: number;
        while ((sep = buf.indexOf('\n\n')) >= 0) {
          const frame = buf.slice(0, sep);
          buf = buf.slice(sep + 2);
          const line = frame.split('\n').find((l) => l.startsWith('data:'));
          if (line) {
            try {
              this.apply([JSON.parse(line.slice(5).trim()) as SpotTick]);
            } catch {
              /* skip a malformed frame */
            }
          }
        }
      }
    } catch {
      /* aborted (stop) or network error -> fall through to reconnect */
    }
    if (!this.stopped) {
      this.retry = setTimeout(() => void this.stream(), 2000);
    }
  }

  private apply(ticks: SpotTick[]): void {
    if (!ticks.length) return;
    const next = new Map(this.spots());
    for (const t of ticks) {
      const prev = next.get(t.symbol)?.price ?? t.price;
      next.set(t.symbol, { symbol: t.symbol, price: t.price, prev, ts: t.ts });
    }
    this.spots.set(next);
  }
}
