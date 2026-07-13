import { Injectable, computed, inject, signal } from '@angular/core';
import { firstValueFrom } from 'rxjs';
import { ApiService } from '../core/api.service';
import {
  Engine,
  InstrumentKind,
  InstrumentPriceRequest,
  InstrumentTermsheetRequest,
} from '../core/models';

//! The sales workflow state of a blotter line. A quote moves new -> quoting -> quoted as the
//! engine prices it; from a resting `quoted` (or `error`) the salesperson resolves it to a
//! terminal `traded` (dealt on behalf of the client) or `missed` (client dealt elsewhere).
//! Terminal rows are frozen — never re-priced — so the executed quote is preserved.
//!   new     : just created (in a panel), not yet priced
//!   quoting : being priced by the engine (a request is in flight)
//!   quoted  : has a fresh price, awaiting the sales decision
//!   error   : the last pricing attempt failed
//!   traded  : sales dealt it on behalf of the client (terminal)
//!   missed  : the client dealt elsewhere (terminal)
export type BlotterStatus = 'new' | 'quoting' | 'quoted' | 'error' | 'traded' | 'missed';

//! the two frozen, sales-set end states — excluded from every (re-)pricing path.
const TERMINAL_STATES: readonly BlotterStatus[] = ['traded', 'missed'];

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
  status: BlotterStatus;
  pricedAt: Date | null; //!< wall-clock time of the last successful pricing
  error?: string;
}

const GREEK_KEYS = ['delta', 'gamma', 'vega', 'rho', 'theta'] as const;

//! is this row in a frozen, sales-set terminal state (never re-priced)?
export function isTerminal(status: BlotterStatus): boolean {
  return TERMINAL_STATES.includes(status);
}

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
  //! in-memory, per-session guard: the demo book is seeded at most once per app load,
  //! and only when the blotter is empty. Deliberately NOT persisted — a stale localStorage
  //! flag from an earlier (possibly failed) launch must never permanently suppress the demo.
  //! Once seeded, the rows persist (STORE_KEY), so a later reload restores them instead of
  //! re-seeding; a genuinely empty blotter on a fresh load seeds again.
  private demoSeeded = false;

  constructor() {
    this.restore();
  }

  //! First-launch demo: generate `count` random contracts (vanilla / barrier / variance /
  //! Asian / ratchet …) on the given **equity** underlyings and add them to the blotter, so
  //! a fresh install lands on a populated monitoring book instead of a blank tab. No-op if
  //! the blotter already has rows or the demo was already seeded this session. Each
  //! underlying carries its spot so barriers can book absolute cash levels (the engine's
  //! barrier levels are always absolute — a relative % is meaningless there).
  seedDemo(
    workspaceId: string,
    underlyings: DemoUnderlying[],
    currency: string,
    today: string,
    count = 10,
  ): void {
    if (!underlyings.length || this.rows().length || this.demoSeeded) return;
    this.demoSeeded = true;
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

  //! Create a blotter line (status `new`, deep-copying the request so later panel edits don't
  //! mutate the monitored row) WITHOUT pricing it, and return its id. The pricing is the
  //! caller's choice — `add` prices immediately; a panel upsert supplies its own fresh quote.
  private createRow(input: { label: string; kind: InstrumentKind; request: InstrumentPriceRequest }): string {
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
      status: 'new',
      pricedAt: null,
    };
    this.rows.update((rs) => [...rs, row]);
    this.persist();
    return row.id;
  }

  //! Add a product to the blotter and price it once immediately so it shows a quote. Returns
  //! the new row id so a caller (a pricing panel) can keep updating the same line.
  add(input: { label: string; kind: InstrumentKind; request: InstrumentPriceRequest }): string {
    const id = this.createRow(input);
    void this.priceRow(id, this.liveMode());
    return id;
  }

  //! Upsert a pricing panel's working quote as a SINGLE blotter line, so that every option
  //! being priced in a panel is systematically mirrored here (the sales workflow starts in
  //! the blotter, not on an explicit "send"). The panel keeps the returned id and passes it
  //! back on each re-price so it updates the same row instead of spawning a new one. The panel
  //! supplies the phase and, once priced, the computed quote — the blotter does NOT re-price
  //! here (the panel already called the engine). A fresh row is created when there is no live
  //! link yet or the previously linked row was resolved (traded/missed) — a new quote then
  //! opens a new line rather than reviving a closed deal.
  upsertFromPanel(input: {
    rowId: string | null;
    label: string;
    kind: InstrumentKind;
    request: InstrumentPriceRequest;
    status: Extract<BlotterStatus, 'quoting' | 'quoted' | 'error'>;
    premium?: number | null;
    greeks?: BlotterRow['greeks'];
    currency?: string;
    error?: string;
  }): string {
    const existing = input.rowId ? this.rows().find((r) => r.id === input.rowId) : undefined;
    if (!existing || isTerminal(existing.status)) {
      // no live link (or the old one is closed): open a new line (no re-price — the panel
      // already priced), then fold in the panel's own fresh result + phase.
      const id = this.createRow({ label: input.label, kind: input.kind, request: input.request });
      this.applyPanelQuote(id, input);
      return id;
    }
    this.applyPanelQuote(existing.id, input);
    return existing.id;
  }

  //! fold a panel-supplied quote (phase + premium/greeks) into an existing row.
  private applyPanelQuote(
    id: string,
    input: {
      label: string;
      request: InstrumentPriceRequest;
      status: Extract<BlotterStatus, 'quoting' | 'quoted' | 'error'>;
      premium?: number | null;
      greeks?: BlotterRow['greeks'];
      currency?: string;
      error?: string;
    },
  ): void {
    this.rows.update((rs) =>
      rs.map((r) => {
        if (r.id !== id) return r;
        const request = { ...input.request, instrument: { ...input.request.instrument }, live: false };
        if (input.status === 'quoting') {
          return { ...r, label: input.label, request, status: 'quoting', error: undefined };
        }
        if (input.status === 'error') {
          return { ...r, label: input.label, request, status: 'error', error: input.error };
        }
        const prev = r.premium;
        const premium = input.premium ?? null;
        const dir = prev != null && premium != null && Number.isFinite(premium) ? Math.sign(premium - prev) : 0;
        return {
          ...r,
          label: input.label,
          request,
          prevPremium: prev,
          premium,
          dir,
          greeks: input.greeks ?? {},
          currency: input.currency ?? r.currency,
          status: 'quoted',
          pricedAt: new Date(),
          error: undefined,
        };
      }),
    );
    this.persist();
  }

  //! Sales resolves a row to a terminal state: `traded` (dealt on behalf of the client) or
  //! `missed` (client dealt elsewhere). Freezes the last quote — the row is never re-priced
  //! again. `reopen` undoes a mis-click, returning the row to `quoted` and re-pricing it.
  markTraded(id: string): void {
    this.setStatus(id, 'traded');
  }
  markMissed(id: string): void {
    this.setStatus(id, 'missed');
  }
  reopen(id: string): void {
    this.setStatus(id, 'quoted');
    void this.priceRow(id, false);
  }
  private setStatus(id: string, status: BlotterStatus): void {
    this.rows.update((rs) => rs.map((r) => (r.id === id ? { ...r, status } : r)));
    this.persist();
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

  //! one-off re-price of every (non-terminal) row, off the live feed (used to re-quote
  //! restored rows). Terminal rows keep their frozen executed quote.
  async repriceAll(live = false): Promise<void> {
    await Promise.all(this.rows().filter((r) => !isTerminal(r.status)).map((r) => this.priceRow(r.id, live)));
  }

  //! re-price the TARGETED rows once (the ticked ones, or the whole book if none
  //! ticked) — the toolbar "Re-price" button. Skips frozen terminal rows.
  async repriceSelected(live = false): Promise<void> {
    await Promise.all(
      this.targets().filter((r) => !isTerminal(r.status)).map((r) => this.priceRow(r.id, live)),
    );
  }

  //! render ONE row's termsheet (the engine's !termsheet task) and download it as a
  //! Markdown file named from the row label. Returns false if the render/request failed
  //! (the caller surfaces it). Per-row so each product's termsheet is a self-contained file.
  async downloadRowTermsheet(row: BlotterRow): Promise<boolean> {
    const req: InstrumentTermsheetRequest = {
      workspaceId: row.request.workspaceId,
      kind: row.kind,
      instrument: row.request.instrument,
      title: row.label,
    };
    try {
      const res = await firstValueFrom(this.api.instrumentTermsheet(req));
      const blob = new Blob([res.termsheet.trim() + '\n'], { type: 'text/markdown;charset=utf-8' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = termsheetFilename(row.label);
      a.click();
      URL.revokeObjectURL(url);
      return true;
    } catch {
      return false;
    }
  }

  private async liveTick(): Promise<void> {
    if (!this.liveMode()) return;
    await Promise.all(this.rows().filter((r) => !isTerminal(r.status)).map((r) => this.priceRow(r.id, true)));
    if (this.liveMode()) {
      const ms = Math.max(1, this.liveThrottleSec) * 1000;
      this.liveTimer = setTimeout(() => void this.liveTick(), ms);
    }
  }

  //! Price one row by id and fold the quote back in (tracking the premium move direction for
  //! the live tint). The row may have been removed mid-flight, so re-resolve it by id.
  //! Terminal rows (traded / missed) are frozen and skipped. Flips the row to `quoting` while
  //! the request is in flight, then to `quoted` (or `error`).
  private async priceRow(id: string, live: boolean): Promise<void> {
    const row = this.rows().find((r) => r.id === id);
    if (!row || isTerminal(row.status)) return;
    this.rows.update((rs) => rs.map((r) => (r.id === id ? { ...r, status: 'quoting' } : r)));
    try {
      const res = await firstValueFrom(this.api.priceInstrument({ ...row.request, live }));
      this.rows.update((rs) =>
        rs.map((r) => {
          if (r.id !== id || isTerminal(r.status)) return r;
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
            status: 'quoted',
            pricedAt: new Date(),
            error: undefined,
          };
        }),
      );
    } catch (e) {
      const err = e as { error?: { message?: string } };
      this.rows.update((rs) =>
        rs.map((r) =>
          r.id === id && !isTerminal(r.status)
            ? { ...r, status: 'error', error: err.error?.message ?? 'Pricing failed' }
            : r,
        ),
      );
    }
  }

  //! Persist the monitored set so a reload restores the book. The request + label rebuild the
  //! line; the workflow `status` and last quote (premium/greeks/currency) are persisted too so
  //! sales-set terminal rows (traded / missed) survive a reload with their executed quote
  //! frozen. Non-terminal rows are re-quoted live on restore. Best-effort — swallow storage
  //! errors. A stray transient `quoting` is normalised to `new` (no request is in flight after
  //! a reload).
  private persist(): void {
    try {
      const slim = this.rows().map((r) => ({
        id: r.id,
        label: r.label,
        kind: r.kind,
        request: r.request,
        status: r.status === 'quoting' ? 'new' : r.status,
        premium: r.premium,
        greeks: r.greeks,
        currency: r.currency,
        pricedAt: r.pricedAt ? r.pricedAt.getTime() : null,
      }));
      localStorage.setItem(this.STORE_KEY, JSON.stringify(slim));
    } catch {
      /* storage full / unavailable — persistence is best-effort */
    }
  }

  private restore(): void {
    type Saved = Pick<BlotterRow, 'id' | 'label' | 'kind' | 'request'> &
      Partial<Pick<BlotterRow, 'status' | 'premium' | 'greeks' | 'currency'>> & { pricedAt?: number | null };
    let saved: Saved[] = [];
    try {
      const raw = localStorage.getItem(this.STORE_KEY);
      if (raw) saved = JSON.parse(raw);
    } catch {
      saved = [];
    }
    if (!saved.length) return;
    this.rows.set(
      saved.map((s) => ({
        id: s.id,
        label: s.label,
        kind: s.kind,
        request: s.request,
        premium: s.premium ?? null,
        prevPremium: null,
        dir: 0,
        greeks: s.greeks ?? {},
        currency: s.currency ?? s.request.currency ?? '',
        status: s.status ?? 'new',
        pricedAt: s.pricedAt ? new Date(s.pricedAt) : null,
      })),
    );
    // re-quote the restored (non-terminal) rows once so they aren't blank; terminal rows keep
    // their frozen executed quote.
    void this.repriceAll(false);
  }
}

//! Build a safe termsheet download filename from a row label: sanitize to
//! `[A-Za-z0-9._-]` (spaces -> '_'), never trust it for path/extension trickery,
//! always end in `.md`. Mirrors the pricing panels' own filename derivation.
function termsheetFilename(label: string): string {
  let name = (label || 'termsheet').trim().replace(/\s+/g, '_').replace(/[^A-Za-z0-9._-]/g, '');
  if (!name) name = 'termsheet';
  return name.toLowerCase().endsWith('.md') ? name : `${name}.md`;
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

//! One equity underlying available to the demo seed: its name and current spot
//! (the spot lets barriers book absolute cash levels — see randomContract).
export interface DemoUnderlying {
  name: string;
  spot: number;
}

//! One random contract on a random (equity) underlying: the `instrument` fields the
//! panels build, a human label, the kind, and the ENGINE to price it on (path-dependent
//! notes — Asian / ratchet — and American vanillas are Monte-Carlo only; the rest use
//! ana). Strikes are booked as a PERCENT of spot (relative, spot-agnostic). Barrier
//! LEVELS, however, are always absolute in the engine (Barrier::SetToday keeps them in
//! cash), so they are booked as an absolute price = percent × spot here — otherwise a
//! bare "140" would be a cash level far from a real spot and produce a degenerate barrier
//! the closed form rejects. The demo stays on equities so every (kind, engine) combo has
//! a griddable underlying (ANA barriers/varswaps need one) and a diffusable vol surface.
function randomContract(
  underlyings: DemoUnderlying[],
  today: string,
): { label: string; kind: InstrumentKind; instrument: Record<string, unknown>; engine: Engine } {
  const u = pick(underlyings);
  const kind = pick([
    'vanilla',
    'barrier',
    'variance',
    'asian',
    'ratchet',
    'digital',
  ] as const);
  const maturity = maturityFrom(today, pick([6, 12, 18, 24, 36]));

  if (kind === 'variance') {
    const volStrike = 15 + Math.floor(Math.random() * 25); // 15..39 %
    return {
      kind,
      engine: 'ana',
      label: `${u.name} var swap K=${volStrike}% ${maturity}`,
      instrument: { underlying: u.name, maturity, volatility_strike: volStrike, notional: 10000 },
    };
  }

  const type = pick(['call', 'put'] as const);
  const strike = pick([80, 90, 95, 100, 105, 110, 120]); // percent of spot

  if (kind === 'asian') {
    //! path-dependent -> Monte-Carlo only
    return {
      kind,
      engine: 'mcl',
      label: `${u.name} Asian ${type} ${strike}% ${maturity}`,
      instrument: {
        underlying: u.name,
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
      label: `${u.name} ratchet [-${cap},${cap}]% ${maturity}`,
      instrument: {
        underlying: u.name,
        maturity,
        nominal: 100,
        observation_period_days: 90,
        local_floor: -cap,
        local_cap: cap,
        global_floor: 0,
      },
    };
  }

  if (kind === 'digital') {
    //! European binary — path-independent, closed-form ANA
    const payout = pick(['cash_or_nothing', 'asset_or_nothing'] as const);
    return {
      kind,
      engine: 'ana',
      label: `${u.name} ${payout === 'cash_or_nothing' ? 'digital' : 'asset-digital'} ${type} ${strike}% ${maturity}`,
      instrument: {
        underlying: u.name,
        strike,
        is_absolute_strike: false,
        maturity,
        type,
        payout,
        ...(payout === 'cash_or_nothing' ? { cash_amount: 1 } : {}),
      },
    };
  }

  if (kind === 'barrier') {
    const barrierType = pick(['up&out', 'up&in', 'down&out', 'down&in'] as const);
    const isDown = barrierType.startsWith('down');
    const levelPct = isDown ? pick([60, 70, 80]) : pick([120, 130, 140]); // percent of spot
    const barrierLevel = (levelPct / 100) * u.spot; //!< engine barrier levels are absolute cash
    return {
      kind,
      engine: 'ana',
      label: `${u.name} ${type} ${strike}% ${barrierType} ${levelPct}% ${maturity}`,
      instrument: {
        underlying: u.name,
        strike,
        is_absolute_strike: false, //!< strike is % of spot (resolved in the engine)
        maturity,
        type,
        nominal: 1,
        barrier_type: barrierType,
        barrier_monitoring_type: 'continuous_monitoring',
        [isDown ? 'barrier_down_level' : 'barrier_up_level']: barrierLevel,
      },
    };
  }

  const exercise = pick(['european', 'american'] as const);
  return {
    kind,
    engine: exercise === 'american' ? 'mcl' : 'ana', //!< american vanilla has no ANA closed form
    label: `${u.name} ${type} ${strike}% ${maturity} (${exercise})`,
    instrument: { underlying: u.name, strike, is_absolute_strike: false, maturity, type, exercise, nominal: 1 },
  };
}
