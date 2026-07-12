import { Injectable, computed, inject, signal } from '@angular/core';
import { firstValueFrom } from 'rxjs';
import { ApiService } from '../core/api.service';
import {
  Engine,
  InstrumentKind,
  InstrumentPriceRequest,
  InstrumentTermsheetRequest,
} from '../core/models';

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

  //! per-row tick selection (by id). The toolbar actions (re-price / termsheet)
  //! operate on the ticked rows, or on the WHOLE book when nothing is ticked —
  //! so the buttons stay useful without forcing a selection.
  readonly selected = signal<Set<string>>(new Set());
  readonly selectedCount = computed(() => {
    const ids = this.selected();
    return this.rows().filter((r) => ids.has(r.id)).length; //!< ignore stale ids
  });
  readonly allSelected = computed(
    () => this.count() > 0 && this.selectedCount() === this.count(),
  );
  readonly someSelected = computed(
    () => this.selectedCount() > 0 && !this.allSelected(),
  );

  isSelected(id: string): boolean {
    return this.selected().has(id);
  }

  toggleSelect(id: string): void {
    this.selected.update((s) => {
      const next = new Set(s);
      if (next.has(id)) {
        next.delete(id);
      } else {
        next.add(id);
      }
      return next;
    });
  }

  //! header checkbox: select all when not all are selected, else clear.
  toggleSelectAll(): void {
    this.selected.set(this.allSelected() ? new Set() : new Set(this.rows().map((r) => r.id)));
  }

  //! the rows an action targets: the ticked ones, or the whole book if none ticked.
  private targets(): BlotterRow[] {
    const ids = this.selected();
    const ticked = this.rows().filter((r) => ids.has(r.id));
    return ticked.length ? ticked : this.rows();
  }

  //! which Greek columns to show: the union of Greeks present across the rows, in canonical
  //! order, so a blotter mixing instruments shows just the columns that carry a value
  //! somewhere (e.g. premium-only rows that were sent without the Greeks toggle).
  readonly greekColumns = computed<string[]>(() => {
    const present = new Set<string>();
    for (const r of this.rows()) for (const g of GREEK_KEYS) if (r.greeks[g] != null) present.add(g);
    return GREEK_KEYS.filter((g) => present.has(g));
  });

  private readonly STORE_KEY = 'thoth.blotter';
  //! set once the first-launch demo book has been generated, so it is never
  //! re-seeded (clearing the blotter on purpose must not bring it back).
  private readonly SEEDED_KEY = 'thoth.blotter.seeded';

  constructor() {
    this.restore();
  }

  //! whether the first-launch demo book has already been generated (or the user
  //! has any persisted rows — an existing book is proof of a prior visit).
  private alreadySeeded(): boolean {
    try {
      return localStorage.getItem(this.SEEDED_KEY) === '1';
    } catch {
      return false;
    }
  }

  //! First-launch demo: generate `count` random contracts (vanilla / barrier /
  //! variance swap) on the given underlyings and add them to the blotter, so a
  //! fresh install lands on a populated monitoring book instead of a blank tab.
  //! No-op if the blotter is non-empty or the demo has already run once.
  seedDemo(
    workspaceId: string,
    underlyings: string[],
    currency: string,
    today: string,
    count = 10,
  ): void {
    if (!underlyings.length || this.rows().length || this.alreadySeeded()) return;
    try {
      localStorage.setItem(this.SEEDED_KEY, '1');
    } catch {
      /* storage unavailable — still seed this session, just don't persist the flag */
    }
    for (let i = 0; i < count; i++) {
      const { label, kind, instrument, engine } = randomContract(underlyings, today);
      const request: InstrumentPriceRequest = {
        workspaceId,
        engine,
        kind,
        instrument: { ...instrument, premium_currency: currency },
        indicators: ['premium', 'delta', 'gamma', 'vega', 'rho', 'theta'],
        currency,
      };
      this.add({ label, kind, request });
    }
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
    this.selected.update((s) => {
      const next = new Set(s);
      next.delete(id);
      return next;
    });
    this.persist();
  }

  clear(): void {
    this.stopLive();
    this.rows.set([]);
    this.selected.set(new Set());
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

  //! one-off re-price of every row, off the live feed (used to re-quote restored rows).
  async repriceAll(live = false): Promise<void> {
    await Promise.all(this.rows().map((r) => this.priceRow(r.id, live)));
  }

  //! re-price the TARGETED rows once (the ticked ones, or the whole book if none
  //! ticked) — the toolbar "Re-price" button.
  async repriceSelected(live = false): Promise<void> {
    await Promise.all(this.targets().map((r) => this.priceRow(r.id, live)));
  }

  //! render the termsheets of the targeted rows and download them as ONE Markdown
  //! file (sections separated by a horizontal rule) — a single download instead of
  //! N browser save dialogs. Returns the number of documents rendered.
  async downloadTermsheets(): Promise<{ ok: number; failed: number }> {
    const rows = this.targets();
    let ok = 0;
    let failed = 0;
    const sections: string[] = [];
    for (const r of rows) {
      const req: InstrumentTermsheetRequest = {
        workspaceId: r.request.workspaceId,
        kind: r.kind,
        instrument: r.request.instrument,
        title: r.label,
      };
      try {
        const res = await firstValueFrom(this.api.instrumentTermsheet(req));
        sections.push(res.termsheet.trim());
        ok++;
      } catch {
        failed++;
      }
    }
    if (sections.length) {
      const blob = new Blob([sections.join('\n\n---\n\n') + '\n'], {
        type: 'text/markdown;charset=utf-8',
      });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = sections.length === 1 ? 'termsheet.md' : `termsheets_${sections.length}.md`;
      a.click();
      URL.revokeObjectURL(url);
    }
    return { ok, failed };
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

//! --- first-launch demo book: random contract generation ---------------------

function pick<T>(xs: readonly T[]): T {
  return xs[Math.floor(Math.random() * xs.length)];
}

//! a maturity `months` out from `today` (YYYY-MM-DD), returned in the same format.
function maturityFrom(today: string, months: number): string {
  const d = new Date(`${today}T00:00:00`);
  d.setMonth(d.getMonth() + months);
  const p = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
}

//! One random contract on a random underlying: the `instrument` fields the panels
//! build, a human label, the kind, and the ENGINE to price it on (path-dependent
//! notes — Asian / ratchet — are Monte-Carlo only; the rest default to ana). The
//! strike/barrier levels are booked as a PERCENT of spot (relative) so the sample
//! is spot-agnostic across whatever underlyings exist.
function randomContract(
  underlyings: string[],
  today: string,
): { label: string; kind: InstrumentKind; instrument: Record<string, unknown>; engine: Engine } {
  const u = pick(underlyings);
  const kind = pick([
    'vanilla',
    'barrier',
    'variance_swap',
    'asian',
    'ratchet',
  ] as const);
  const maturity = maturityFrom(today, pick([6, 12, 18, 24, 36]));

  if (kind === 'variance_swap') {
    const volStrike = 15 + Math.floor(Math.random() * 25); // 15..39 %
    return {
      kind,
      engine: 'ana',
      label: `${u} var swap K=${volStrike}% ${maturity}`,
      instrument: { underlying: u, maturity, volatility_strike: volStrike, notional: 10000 },
    };
  }

  const type = pick(['call', 'put'] as const);
  const strike = pick([80, 90, 95, 100, 105, 110, 120]); // percent of spot

  if (kind === 'asian') {
    //! path-dependent -> Monte-Carlo only
    return {
      kind,
      engine: 'mcl',
      label: `${u} Asian ${type} ${strike}% ${maturity}`,
      instrument: {
        underlying: u,
        strike,
        is_absolute_strike: false,
        maturity,
        type,
        nominal: 1,
        observation_period_days: 30,
      },
    };
  }

  if (kind === 'ratchet') {
    //! path-dependent -> Monte-Carlo only
    const cap = pick([4, 5, 6, 8]);
    return {
      kind,
      engine: 'mcl',
      label: `${u} ratchet [-${cap},${cap}]% ${maturity}`,
      instrument: {
        underlying: u,
        maturity,
        nominal: 100,
        observation_period_days: 90,
        local_floor: -cap,
        local_cap: cap,
        global_floor: 0,
      },
    };
  }

  if (kind === 'barrier') {
    const barrierType = pick(['up&out', 'up&in', 'down&out', 'down&in'] as const);
    const isDown = barrierType.startsWith('down');
    const level = isDown ? pick([60, 70, 80]) : pick([120, 130, 140]); // percent of spot
    return {
      kind,
      engine: 'ana',
      label: `${u} ${type} ${strike}% ${barrierType} ${level}% ${maturity}`,
      instrument: {
        underlying: u,
        strike,
        maturity,
        type,
        nominal: 1,
        barrier_type: barrierType,
        barrier_monitoring_type: 'continuous_monitoring',
        [isDown ? 'barrier_down_level' : 'barrier_up_level']: level,
      },
    };
  }

  const exercise = pick(['european', 'american'] as const);
  return {
    kind,
    engine: exercise === 'american' ? 'mcl' : 'ana', //!< american vanilla has no ANA closed form
    label: `${u} ${type} ${strike}% ${maturity} (${exercise})`,
    instrument: { underlying: u, strike, is_absolute_strike: false, maturity, type, exercise, nominal: 1 },
  };
}
