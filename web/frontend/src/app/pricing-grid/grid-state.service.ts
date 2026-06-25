import { Injectable, computed, inject, signal } from '@angular/core';
import { Subscription, interval, switchMap } from 'rxjs';
import { ApiService } from '../core/api.service';
import {
  Engine,
  Exercise,
  GridMatrix,
  GridMeta,
  GridProgress,
  GridSubmit,
  OptionChain,
  OptionType,
  Workspace,
  WsObject,
} from '../core/models';

const GREEK_INDICATORS = ['delta', 'gamma', 'vega', 'rho', 'theta'];

//! localStorage key prefix; the form snapshot is kept per workspace so switching
//! workspaces (or users on the same browser) doesn't cross-contaminate.
const STORE_PREFIX = 'thoth.pricing-grid.';

//! What we persist across a reconnection (reload / re-login): the form inputs plus the
//! last submitted job id. Results aren't stored — they live server-side (GridRun) and are
//! re-fetched by id on restore, so a reconnection shows the same grid the engine produced.
interface GridSnapshot {
  engine: Engine | null;
  selectedUnderlyings: string[];
  types: OptionType[];
  strikesText: string;
  maturities: string[];
  exercise: Exercise;
  includeGreeks: boolean;
  currency: string;
  lastJobId: string | null;
}

//! Root-scoped state for the Pricing Grid. Living in a singleton (not the component) means
//! the form, results and an in-flight job survive tab navigation — the component is just a
//! view over this. Polling runs here too, so a running job keeps updating while you're away.
@Injectable({ providedIn: 'root' })
export class GridStateService {
  private readonly api = inject(ApiService);
  private poll?: Subscription;
  private loaded = false;

  // loaded context
  readonly workspace = signal<Workspace | null>(null);
  readonly objects = signal<WsObject[]>([]);

  // form state (mutable for [(ngModel)])
  engine: Engine | null = null;
  selectedUnderlyings: string[] = [];
  types: OptionType[] = ['call'];
  strikesText = '';
  maturities: string[] = []; //!< YYYY-MM-DD, populated via the date picker
  exercise: Exercise = 'european';
  includeGreeks = true;
  currency = '';

  private lastJobId: string | null = null;

  // results
  readonly status = signal<string | null>(null);
  readonly progress = signal<GridProgress['progress']>(null);
  readonly running = signal(false);
  readonly matrices = signal<GridMatrix[]>([]);
  readonly meta = signal<GridMeta | null>(null);
  readonly error = signal<string | null>(null);

  //! Pivot the flat (underlying, type) matrices into one option chain per underlying, so the
  //! view can render a per-maturity calls|strike|puts block. Underlying order follows first
  //! appearance in matrices() (which tracks the request's underlying order).
  readonly chains = computed<OptionChain[]>(() => {
    const byUnderlying = new Map<string, OptionChain>();
    for (const m of this.matrices()) {
      let chain = byUnderlying.get(m.underlying);
      if (!chain) {
        chain = { underlying: m.underlying, currency: m.currency, strikes: m.strikes, maturities: m.maturities };
        byUnderlying.set(m.underlying, chain);
      }
      if (m.type === 'call') chain.call = m;
      else chain.put = m;
    }
    return [...byUnderlying.values()];
  });

  readonly underlyingObjects = computed(() =>
    this.objects().filter((o) => ['equity', 'basket', 'forex', 'rainbow', 'composite'].includes(o.kind)),
  );
  readonly currencyNames = computed(() =>
    this.objects().filter((o) => o.kind === 'currency').map((o) => o.name),
  );

  //! Resolve the default workspace + load its objects once; on later tab entries just
  //! re-pull objects (so Underlyings tracks saved market data) without clobbering results.
  init(): void {
    if (this.loaded) {
      this.refreshObjects();
      return;
    }
    this.loaded = true;
    this.api.listWorkspaces().subscribe((list) => {
      if (list.length) this.useWorkspace(list[0]);
      else this.api.createWorkspace({ name: 'Default' }).subscribe((ws) => this.useWorkspace(ws));
    });
  }

  private useWorkspace(ws: Workspace): void {
    this.workspace.set(ws);
    if (!this.currency) this.currency = ws.currency;
    // Restore the persisted form + last job BEFORE pulling objects, so refreshObjects
    // (which only fixes an *invalid* currency selection) keeps the restored choices.
    // First-time users (no saved snapshot) get a ready-to-price default grid instead.
    if (!this.restore(ws.id)) this.applyDefaults(ws);
    this.refreshObjects();
  }

  //! First-run form defaults (only when there is no saved snapshot): a ready-to-price
  //! ATM-ish grid — strikes 80..120, the next five monthly maturities, EUR, European,
  //! Greeks on. Engine and underlyings are left for the user to pick. refreshObjects()
  //! still validates the currency against the workspace's actual currencies.
  private applyDefaults(ws: Workspace): void {
    this.engine = 'ana';
    this.strikesText = '80 90 100 110 120';
    this.maturities = [1, 2, 3, 4, 5].map((m) => addMonths(ws.today, m));
    this.exercise = 'european';
    this.includeGreeks = true;
    this.currency = 'eur';
  }

  //! storage key for a workspace's pricing-grid snapshot.
  private storeKey(wsId: string): string {
    return `${STORE_PREFIX}${wsId}`;
  }

  //! Persist the current form + last job id for this workspace. Called on every form
  //! change and on submit; a no-op (swallowed) when localStorage is unavailable.
  persist(): void {
    const ws = this.workspace();
    if (!ws) return;
    const snap: GridSnapshot = {
      engine: this.engine,
      selectedUnderlyings: this.selectedUnderlyings,
      types: this.types,
      strikesText: this.strikesText,
      maturities: this.maturities,
      exercise: this.exercise,
      includeGreeks: this.includeGreeks,
      currency: this.currency,
      lastJobId: this.lastJobId,
    };
    try {
      localStorage.setItem(this.storeKey(ws.id), JSON.stringify(snap));
    } catch {
      /* storage full / unavailable — persistence is best-effort */
    }
  }

  //! Re-hydrate the form from localStorage and, if a job was in flight or finished,
  //! re-fetch it by id (the GridRun survives server-side) to restore its results/status.
  private restore(wsId: string): boolean {
    let snap: GridSnapshot | null = null;
    try {
      const raw = localStorage.getItem(this.storeKey(wsId));
      if (raw) snap = JSON.parse(raw) as GridSnapshot;
    } catch {
      snap = null;
    }
    if (!snap) return false;

    this.engine = snap.engine ?? null;
    this.selectedUnderlyings = snap.selectedUnderlyings ?? [];
    this.types = snap.types ?? ['call'];
    this.strikesText = snap.strikesText ?? '';
    this.maturities = snap.maturities ?? [];
    this.exercise = snap.exercise ?? 'european';
    this.includeGreeks = snap.includeGreeks ?? true;
    if (snap.currency) this.currency = snap.currency;
    this.lastJobId = snap.lastJobId ?? null;

    if (this.lastJobId) this.rehydrateJob(this.lastJobId);
    return true;
  }

  //! Pull a previously-submitted run back into view: show its results if done, resume
  //! polling if still queued/running, surface its error otherwise. A vanished run (404)
  //! is ignored so a stale id never blocks the form.
  private rehydrateJob(jobId: string): void {
    this.api.getGrid(jobId).subscribe({
      next: (res) => {
        this.status.set(res.status);
        if (res.status === 'done') {
          this.matrices.set(res.result?.matrices ?? []);
          this.meta.set(res.result?.meta ?? null);
        } else if (res.status === 'error') {
          this.error.set(res.error ?? 'Pricing failed');
        } else {
          // queued / running — reattach to the live job
          this.running.set(true);
          this.startPolling(jobId);
        }
      },
      error: () => {
        // run no longer exists; drop the stale id so it isn't retried
        this.lastJobId = null;
        this.persist();
      },
    });
  }

  refreshObjects(): void {
    const ws = this.workspace();
    if (!ws) return;
    this.api.listObjects(ws.id).subscribe((objs) => {
      this.objects.set(objs);
      // keep a valid currency selection
      if (!this.currency || (this.currencyNames().length && !this.currencyNames().includes(this.currency))) {
        this.currency = this.currencyNames().includes(ws.currency) ? ws.currency : (this.currencyNames()[0] ?? ws.currency);
      }
    });
  }

  setEngine(e: Engine): void {
    this.engine = e;
    //! every engine (incl. CPU mcl, via single-tree per-contract attribution) now produces
    //! per-cell Greeks, so the choice no longer touches the Greeks preference.
    this.persist();
  }

  toggleType(t: OptionType, checked: boolean): void {
    this.types = checked ? [...this.types, t] : this.types.filter((x) => x !== t);
    this.persist();
  }

  private parseNumbers(text: string): number[] {
    return text.split(/[\s,]+/).map((s) => s.trim()).filter(Boolean).map(Number).filter((n) => Number.isFinite(n));
  }

  //! add a maturity from the date picker (kept sorted & de-duplicated as YYYY-MM-DD).
  addMaturity(d: Date | null): void {
    if (!d) return;
    const iso = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
    if (!this.maturities.includes(iso)) this.maturities = [...this.maturities, iso].sort();
    this.persist();
  }
  removeMaturity(iso: string): void {
    this.maturities = this.maturities.filter((m) => m !== iso);
    this.persist();
  }

  get canSubmit(): boolean {
    return (
      !!this.workspace() &&
      !!this.engine &&
      this.selectedUnderlyings.length > 0 &&
      this.types.length > 0 &&
      this.parseNumbers(this.strikesText).length > 0 &&
      this.maturities.length > 0 &&
      !this.running()
    );
  }

  submit(): void {
    const ws = this.workspace();
    if (!ws || !this.engine) return;

    const indicators = ['premium'];
    if (this.includeGreeks) indicators.push(...GREEK_INDICATORS); // every engine now does per-cell Greeks

    const dto: GridSubmit = {
      workspaceId: ws.id,
      engine: this.engine,
      underlyings: this.selectedUnderlyings,
      types: this.types,
      strikes: this.parseNumbers(this.strikesText),
      maturities: this.maturities,
      indicators,
      exercise: this.exercise,
      currency: this.currency || ws.currency,
    };

    this.error.set(null);
    this.matrices.set([]);
    this.meta.set(null);
    this.running.set(true);
    this.status.set('queued');

    this.api.submitGrid(dto).subscribe({
      next: ({ jobId }) => {
        this.lastJobId = jobId;
        this.persist(); // remember the in-flight job so a reconnection can reattach
        this.startPolling(jobId);
      },
      error: (e) => this.fail(e),
    });
  }

  private startPolling(jobId: string): void {
    this.poll?.unsubscribe();
    this.poll = interval(800)
      .pipe(switchMap(() => this.api.getGridProgress(jobId)))
      .subscribe({
        next: (p) => {
          this.status.set(p.status);
          this.progress.set(p.progress);
          if (p.status === 'done' || p.status === 'error') {
            this.poll?.unsubscribe();
            this.fetchResult(jobId);
          }
        },
        error: (e) => this.fail(e),
      });
  }

  private fetchResult(jobId: string): void {
    this.api.getGrid(jobId).subscribe({
      next: (res) => {
        this.running.set(false);
        if (res.status === 'error') {
          this.error.set(res.error ?? 'Pricing failed');
        } else {
          this.matrices.set(res.result?.matrices ?? []);
          this.meta.set(res.result?.meta ?? null);
        }
      },
      error: (e) => this.fail(e),
    });
  }

  private fail(e: unknown): void {
    this.poll?.unsubscribe();
    this.running.set(false);
    const err = e as { error?: { message?: string } };
    this.error.set(err.error?.message ?? 'Request failed');
  }

  get progressPct(): number {
    const p = this.progress();
    if (!p || !p.total) return 0;
    return Math.round((p.current / p.total) * 100);
  }
}

//! add `months` calendar months to a YYYY-MM-DD date, clamping the day to the target
//! month's length (so e.g. Jan 31 + 1 month -> Feb 28/29 rather than spilling into March).
function addMonths(iso: string, months: number): string {
  const [y, m, d] = iso.split('-').map(Number);
  const target = m - 1 + months;
  const year = y + Math.floor(target / 12);
  const month = ((target % 12) + 12) % 12;
  const lastDay = new Date(Date.UTC(year, month + 1, 0)).getUTCDate();
  const day = Math.min(d, lastDay);
  return `${String(year).padStart(4, '0')}-${String(month + 1).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
}
