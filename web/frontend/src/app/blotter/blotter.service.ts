import { Injectable, computed, inject, signal } from '@angular/core';
import { firstValueFrom } from 'rxjs';
import { ApiService } from '../core/api.service';
import { InstrumentKind, InstrumentPriceRequest } from '../core/models';

//! One monitored line in the global blotter: the exact pricing request that produced it
//! (so it can be re-priced — live or on demand) plus its last quote and the move direction
//! of its premium (for the green/red live tint, same idea as the option chain).
export interface BlotterRow {
  id: string;
  label: string; //!< human summary, e.g. "ACME call 100 2026-12-31"
  kind: InstrumentKind;
  request: InstrumentPriceRequest;
  premium: number | null;
  prevPremium: number | null;
  dir: number; //!< sign of the last premium move: +1 up / -1 down / 0 flat
  greeks: Partial<Record<'delta' | 'gamma' | 'vega' | 'rho' | 'theta', number>>;
  currency: string;
  status: 'queued' | 'priced' | 'error';
  error?: string;
}

const GREEK_KEYS = ['delta', 'gamma', 'vega', 'rho', 'theta'] as const;

//! Root-scoped global monitoring blotter. Rows pushed from the pricing panels live here so
//! they survive tab navigation; Live mode re-prices every row off the live feed on a throttle
//! (the GAP between sweeps, timed after each completes, so a slow engine never overlaps). The
//! set is persisted to localStorage so a reload restores the monitored book.
@Injectable({ providedIn: 'root' })
export class BlotterService {
  private readonly api = inject(ApiService);

  readonly rows = signal<BlotterRow[]>([]);
  readonly liveMode = signal(false);
  liveThrottleSec = 5;
  private liveTimer?: ReturnType<typeof setTimeout>;
  private seq = 0;

  readonly count = computed(() => this.rows().length);

  //! which Greek columns to show: the union of Greeks present across the rows, in canonical
  //! order, so a blotter mixing vanillas (full Greeks) and variance swaps (premium only)
  //! shows just the columns that carry a value somewhere.
  readonly greekColumns = computed<string[]>(() => {
    const present = new Set<string>();
    for (const r of this.rows()) for (const g of GREEK_KEYS) if (r.greeks[g] != null) present.add(g);
    return GREEK_KEYS.filter((g) => present.has(g));
  });

  private readonly STORE_KEY = 'thoth.blotter';

  constructor() {
    this.restore();
  }

  //! Add a product to the blotter (deep-copying the request so later panel edits don't
  //! mutate the monitored row) and price it once immediately so it shows a quote.
  add(input: { label: string; kind: InstrumentKind; request: InstrumentPriceRequest }): void {
    const row: BlotterRow = {
      id: `${Date.now()}-${this.seq++}`,
      label: input.label,
      kind: input.kind,
      request: { ...input.request, instrument: { ...input.request.instrument }, live: false },
      premium: null,
      prevPremium: null,
      dir: 0,
      greeks: {},
      currency: input.request.currency ?? '',
      status: 'queued',
    };
    this.rows.update((rs) => [...rs, row]);
    this.persist();
    void this.priceRow(row.id, this.liveMode());
  }

  remove(id: string): void {
    this.rows.update((rs) => rs.filter((r) => r.id !== id));
    this.persist();
  }

  clear(): void {
    this.stopLive();
    this.rows.set([]);
    this.persist();
  }

  toggleLive(): void {
    if (this.liveMode()) this.stopLive();
    else this.startLive();
  }

  startLive(): void {
    if (!this.rows().length) return;
    this.liveMode.set(true);
    void this.liveTick();
  }

  stopLive(): void {
    this.liveMode.set(false);
    if (this.liveTimer) {
      clearTimeout(this.liveTimer);
      this.liveTimer = undefined;
    }
  }

  //! one-off re-price of every row, off the live feed (used by the manual "Re-price" button).
  async repriceAll(live = false): Promise<void> {
    await Promise.all(this.rows().map((r) => this.priceRow(r.id, live)));
  }

  private async liveTick(): Promise<void> {
    if (!this.liveMode()) return;
    await Promise.all(this.rows().map((r) => this.priceRow(r.id, true)));
    if (this.liveMode()) {
      const ms = Math.max(1, this.liveThrottleSec) * 1000;
      this.liveTimer = setTimeout(() => void this.liveTick(), ms);
    }
  }

  //! Price one row by id and fold the quote back in (tracking the premium move direction for
  //! the live tint). The row may have been removed mid-flight, so re-resolve it by id.
  private async priceRow(id: string, live: boolean): Promise<void> {
    const row = this.rows().find((r) => r.id === id);
    if (!row) return;
    try {
      const res = await firstValueFrom(this.api.priceInstrument({ ...row.request, live }));
      this.rows.update((rs) =>
        rs.map((r) => {
          if (r.id !== id) return r;
          const prev = r.premium;
          const premium = res.result.premium;
          const dir = prev != null && Number.isFinite(premium) ? Math.sign(premium - prev) : 0;
          return {
            ...r,
            prevPremium: prev,
            premium,
            dir,
            greeks: res.result.greeks ?? {},
            currency: res.currency,
            status: 'priced',
            error: undefined,
          };
        }),
      );
    } catch (e) {
      const err = e as { error?: { message?: string } };
      this.rows.update((rs) =>
        rs.map((r) =>
          r.id === id ? { ...r, status: 'error', error: err.error?.message ?? 'Pricing failed' } : r,
        ),
      );
    }
  }

  //! Persist the monitored set (request + label, not the transient quote) so a reload
  //! restores the book; quotes are re-fetched live. Best-effort — swallow storage errors.
  private persist(): void {
    try {
      const slim = this.rows().map((r) => ({
        id: r.id,
        label: r.label,
        kind: r.kind,
        request: r.request,
      }));
      localStorage.setItem(this.STORE_KEY, JSON.stringify(slim));
    } catch {
      /* storage full / unavailable — persistence is best-effort */
    }
  }

  private restore(): void {
    let saved: Array<Pick<BlotterRow, 'id' | 'label' | 'kind' | 'request'>> = [];
    try {
      const raw = localStorage.getItem(this.STORE_KEY);
      if (raw) saved = JSON.parse(raw);
    } catch {
      saved = [];
    }
    if (!saved.length) return;
    this.rows.set(
      saved.map((s) => ({
        ...s,
        premium: null,
        prevPremium: null,
        dir: 0,
        greeks: {},
        currency: s.request.currency ?? '',
        status: 'queued' as const,
      })),
    );
    // re-quote the restored rows once so they aren't blank.
    void this.repriceAll(false);
  }
}
