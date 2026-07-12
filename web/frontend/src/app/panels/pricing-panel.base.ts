import { signal, inject } from '@angular/core';
import { firstValueFrom } from 'rxjs';
import { ApiService } from '../core/api.service';
import {
  Engine,
  InstrumentKind,
  InstrumentPriceRequest,
  InstrumentTermsheetRequest,
} from '../core/models';
import { BlotterService } from '../blotter/blotter.service';
import { PanelContextService } from './panel-context.service';
import { PanelPrefillService } from './panel-prefill.service';

const GREEK_INDICATORS = ['delta', 'gamma', 'vega', 'rho', 'theta'];

export interface PanelMeta {
  server?: string;
  execMs: number;
  engineMs?: number;
  engineVersion?: string;
}

//! Shared behaviour for the three single-instrument panels (vanilla / barrier / variance):
//! engine + currency + Greeks selection, one-off and live re-pricing off the live feed, the
//! result signals, and pushing the current product to the global blotter. Subclasses only
//! supply the instrument-specific fields (buildFields) and the kind/labels — all the pricing
//! plumbing lives here so the three panels stay thin views.
export abstract class PricingPanelBase {
  readonly ctx = inject(PanelContextService); //!< public: panel templates read it directly
  protected readonly api = inject(ApiService);
  protected readonly blotter = inject(BlotterService);
  protected readonly prefillSvc = inject(PanelPrefillService);

  abstract readonly kind: InstrumentKind;

  // shared form state (mutable for [(ngModel)]) — every instrument has an underlying,
  // a maturity and a reporting currency; the rest is supplied by the subclass.
  engine: Engine = 'ana';
  currency = '';
  includeGreeks = true;
  underlying = '';
  maturityDate: Date | null = null;

  //! maturity as the engine's YYYY-MM-DD, or null when unset.
  protected get maturityIso(): string | null {
    const d = this.maturityDate;
    if (!d) return null;
    return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
  }

  // result
  readonly premium = signal<number | null>(null);
  readonly greeks = signal<Partial<Record<string, number>>>({});
  readonly resultCurrency = signal<string>('');
  readonly meta = signal<PanelMeta | null>(null);
  readonly error = signal<string | null>(null);
  readonly busy = signal(false);

  // live mode
  readonly liveMode = signal(false);
  liveThrottleSec = 5;
  private liveTimer?: ReturnType<typeof setTimeout>;

  init(): void {
    this.ctx.init();
    if (!this.currency) this.currency = this.ctx.defaultCurrency();
    // default maturity ~1 year out so a panel is ready to price as soon as an underlying
    // is picked (the workspace `today` drives the engine's valuation date).
    if (!this.maturityDate) {
      const ws = this.ctx.workspace();
      const base = ws ? new Date(`${ws.today}T00:00:00`) : new Date();
      this.maturityDate = new Date(base.getFullYear() + 1, base.getMonth(), base.getDate());
    }
  }

  //! Fill the form from a pending blotter prefill (double-click -> open in panel), if one
  //! targets this panel's kind. Sets the shared fields here (underlying / maturity / currency
  //! / engine), then defers to applyFields for the instrument-specific ones. Call AFTER init()
  //! so it overrides the defaults. Does not auto-price — the user reviews then clicks Price.
  protected applyPrefill(): void {
    const p = this.prefillSvc.consume(this.kind);
    if (!p) return;
    this.engine = p.engine;
    const i = p.instrument;
    if (typeof i['underlying'] === 'string') this.underlying = i['underlying'];
    if (typeof i['maturity'] === 'string') this.maturityDate = new Date(`${i['maturity']}T00:00:00`);
    if (typeof i['premium_currency'] === 'string') this.currency = i['premium_currency'];
    this.applyFields(i);
  }

  //! Subclass hook: map the booked instrument's own fields onto this panel's ngModel props
  //! (the inverse of buildFields). Default no-op; each panel overrides it.
  protected applyFields(_instrument: Record<string, unknown>): void {}

  //! re-default the currency if the picked one vanished (objects reloaded).
  refresh(): void {
    this.ctx.refreshObjects();
    if (!this.currency || !this.ctx.currencyNames().includes(this.currency)) {
      this.currency = this.ctx.defaultCurrency();
    }
  }

  //! the instrument's own fields (underlying, strike, maturity, …), or null if incomplete.
  //! premium_currency is added by buildRequest, so subclasses omit it here.
  protected abstract buildFields(): Record<string, unknown> | null;

  //! a human label for a blotter row, e.g. "ACME call 100 2026-12-31".
  abstract rowLabel(): string;

  //! whether this instrument exposes per-contract Greeks; panels can override to false so the
  //! Greeks toggle is hidden and only premium is requested.
  readonly supportsGreeks: boolean = true;

  private indicators(): string[] {
    const ind = ['premium'];
    if (this.includeGreeks && this.supportsGreeks) ind.push(...GREEK_INDICATORS);
    return ind;
  }

  //! assemble the BFF request from the current form, or null if it is incomplete.
  buildRequest(live: boolean): InstrumentPriceRequest | null {
    const ws = this.ctx.workspace();
    const fields = this.buildFields();
    if (!ws || !fields) return null;
    const currency = this.currency || ws.currency;
    return {
      workspaceId: ws.id,
      engine: this.engine,
      kind: this.kind,
      instrument: { ...fields, premium_currency: currency },
      indicators: this.indicators(),
      currency,
      live,
    };
  }

  get canPrice(): boolean {
    return this.buildRequest(false) !== null;
  }

  //! a one-off price takes over from live mode.
  price(): void {
    const req = this.buildRequest(false);
    if (!req) return;
    this.stopLive();
    void this.run(req);
  }

  private async run(req: InstrumentPriceRequest): Promise<void> {
    this.busy.set(true);
    this.error.set(null);
    try {
      const res = await firstValueFrom(this.api.priceInstrument(req));
      this.applyResult(res);
    } catch (e) {
      this.fail(e);
    } finally {
      this.busy.set(false);
    }
  }

  private applyResult(res: {
    result: { premium: number; greeks: Partial<Record<string, number>> };
    currency: string;
    meta: PanelMeta;
  }): void {
    this.premium.set(res.result.premium);
    this.greeks.set(res.result.greeks ?? {});
    this.resultCurrency.set(res.currency);
    this.meta.set(res.meta);
  }

  // --- live mode: re-price off the live spots every `liveThrottleSec` seconds ---
  toggleLive(): void {
    if (this.liveMode()) this.stopLive();
    else this.startLive();
  }

  startLive(): void {
    if (!this.canPrice) return;
    this.liveMode.set(true);
    this.error.set(null);
    void this.liveTick();
  }

  stopLive(): void {
    this.liveMode.set(false);
    if (this.liveTimer) {
      clearTimeout(this.liveTimer);
      this.liveTimer = undefined;
    }
  }

  private async liveTick(): Promise<void> {
    if (!this.liveMode()) return;
    const req = this.buildRequest(true);
    if (req) {
      try {
        const res = await firstValueFrom(this.api.priceInstrument(req));
        this.applyResult(res);
        this.error.set(null);
      } catch (e) {
        const err = e as { error?: { message?: string } };
        this.error.set(err.error?.message ?? 'Live pricing failed');
      }
    }
    if (this.liveMode()) {
      const ms = Math.max(1, this.liveThrottleSec) * 1000;
      this.liveTimer = setTimeout(() => void this.liveTick(), ms);
    }
  }

  //! render and download the current product's termsheet (the engine's !termsheet
  //! documentation task) as a Markdown file. Reuses the panel's form state: same
  //! contract fields the Price button sends, no pricing involved.
  async downloadTermsheet(): Promise<void> {
    const priced = this.buildRequest(false);
    if (!priced) return;
    const req: InstrumentTermsheetRequest = {
      workspaceId: priced.workspaceId,
      kind: priced.kind,
      instrument: priced.instrument,
      title: this.rowLabel(),
    };
    this.busy.set(true);
    this.error.set(null);
    try {
      const res = await firstValueFrom(this.api.instrumentTermsheet(req));
      const blob = new Blob([res.termsheet], { type: 'text/markdown;charset=utf-8' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      //! Derive the download name CLIENT-SIDE: never trust the server-supplied filename for
      //! `a.download` (it could inject path/extension trickery). Build it from the sanitized
      //! row label (or kind), and only fall back to the server name if it is a plain `*.md`.
      a.download = this.termsheetFilename(res.filename);
      a.click();
      URL.revokeObjectURL(url);
    } catch (e) {
      this.fail(e);
    } finally {
      this.busy.set(false);
    }
  }

  //! build a safe termsheet download filename. Prefer a name derived from the panel's own
  //! row label / kind (sanitized to `[A-Za-z0-9._-]`, spaces -> '_'); only accept the
  //! server's name when it is already a bare `*.md`. Always ends in `.md`.
  private termsheetFilename(serverName?: string): string {
    if (serverName && /^[\w.\-]+\.md$/.test(serverName)) return serverName;
    const raw = (this.rowLabel() || this.kind || 'termsheet').trim();
    let name = raw.replace(/\s+/g, '_').replace(/[^A-Za-z0-9._-]/g, '');
    if (!name) name = 'termsheet';
    return name.toLowerCase().endsWith('.md') ? name : `${name}.md`;
  }

  //! push the current product to the global monitoring blotter.
  addToBlotter(): void {
    const req = this.buildRequest(false);
    if (!req) return;
    this.blotter.add({ label: this.rowLabel(), kind: this.kind, request: req });
  }

  private fail(e: unknown): void {
    const err = e as { error?: { message?: string } };
    this.error.set(err.error?.message ?? 'Request failed');
  }
}

function pad(n: number): string {
  return String(n).padStart(2, '0');
}
