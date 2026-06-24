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
  OptionType,
  Workspace,
  WsObject,
} from '../core/models';

const GREEK_INDICATORS = ['delta', 'gamma', 'vega', 'rho', 'theta'];

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

  // results
  readonly status = signal<string | null>(null);
  readonly progress = signal<GridProgress['progress']>(null);
  readonly running = signal(false);
  readonly matrices = signal<GridMatrix[]>([]);
  readonly meta = signal<GridMeta | null>(null);
  readonly error = signal<string | null>(null);

  readonly mclSelected = computed(() => this.engine === 'mcl');
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
    this.refreshObjects();
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
    if (e === 'mcl') this.includeGreeks = false;
  }

  toggleType(t: OptionType, checked: boolean): void {
    this.types = checked ? [...this.types, t] : this.types.filter((x) => x !== t);
  }

  private parseNumbers(text: string): number[] {
    return text.split(/[\s,]+/).map((s) => s.trim()).filter(Boolean).map(Number).filter((n) => Number.isFinite(n));
  }

  //! add a maturity from the date picker (kept sorted & de-duplicated as YYYY-MM-DD).
  addMaturity(d: Date | null): void {
    if (!d) return;
    const iso = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
    if (!this.maturities.includes(iso)) this.maturities = [...this.maturities, iso].sort();
  }
  removeMaturity(iso: string): void {
    this.maturities = this.maturities.filter((m) => m !== iso);
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
    if (this.engine !== 'mcl' && this.includeGreeks) indicators.push(...GREEK_INDICATORS);

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
      next: ({ jobId }) => this.startPolling(jobId),
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
