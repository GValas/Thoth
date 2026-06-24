import { Component, OnDestroy, computed, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatCardModule } from '@angular/material/card';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatSelectModule } from '@angular/material/select';
import { MatInputModule } from '@angular/material/input';
import { MatButtonModule } from '@angular/material/button';
import { MatButtonToggleModule } from '@angular/material/button-toggle';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MatChipsModule } from '@angular/material/chips';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSnackBar, MatSnackBarModule } from '@angular/material/snack-bar';
import { Subscription, interval, switchMap } from 'rxjs';
import { ApiService } from '../core/api.service';
import {
  Engine,
  GridMatrix,
  GridProgress,
  GridSubmit,
  OptionType,
  Workspace,
  WsObject,
} from '../core/models';
import { WorkspacePickerComponent } from '../shared/workspace-picker.component';
import { MatrixGridComponent } from './matrix-grid.component';

const GREEK_INDICATORS = ['delta', 'gamma', 'vega', 'rho', 'theta'];

//! Pricing-grid builder: workspace + engine (no default) + strike/maturity axes + type +
//! underlyings. For mcl the per-cell Greeks toggle is disabled (CPU MC = book-level
//! Greeks only). Submit -> POST /api/grid, poll until done, render a heatmapped grid per
//! (underlying, type).
@Component({
  selector: 'app-pricing-grid',
  standalone: true,
  imports: [
    FormsModule,
    MatCardModule,
    MatFormFieldModule,
    MatSelectModule,
    MatInputModule,
    MatButtonModule,
    MatButtonToggleModule,
    MatCheckboxModule,
    MatChipsModule,
    MatIconModule,
    MatProgressBarModule,
    MatSnackBarModule,
    WorkspacePickerComponent,
    MatrixGridComponent,
  ],
  templateUrl: './pricing-grid.component.html',
  styleUrl: './pricing-grid.component.scss',
})
export class PricingGridComponent implements OnDestroy {
  private readonly api = inject(ApiService);
  private readonly snack = inject(MatSnackBar);
  private poll?: Subscription;

  readonly workspace = signal<Workspace | null>(null);
  readonly objects = signal<WsObject[]>([]);

  // form state
  engine: Engine | null = null;
  selectedUnderlyings: string[] = [];
  types: OptionType[] = ['call'];
  strikesText = '';
  maturitiesText = '';
  exercise: 'european' | 'american' = 'european';
  includeGreeks = true;

  readonly status = signal<string | null>(null);
  readonly progress = signal<GridProgress['progress']>(null);
  readonly running = signal(false);
  readonly matrices = signal<GridMatrix[]>([]);
  readonly error = signal<string | null>(null);

  //! Per-cell Greeks come only from ana/pde (or GPU mcl). CPU mcl -> book-level only.
  readonly mclSelected = computed(() => this.engine === 'mcl');

  // Underlying object names: kinds with an underlying-like shape. We surface any object
  // whose kind looks like an underlying (equity/basket/forex/rainbow/composite).
  readonly underlyingObjects = computed(() =>
    this.objects().filter((o) =>
      ['equity', 'basket', 'forex', 'rainbow', 'composite'].includes(o.kind),
    ),
  );

  onWorkspace(ws: Workspace): void {
    this.workspace.set(ws);
    this.selectedUnderlyings = [];
    this.matrices.set([]);
    this.api.listObjects(ws.id).subscribe((objs) => this.objects.set(objs));
  }

  setEngine(e: Engine): void {
    this.engine = e;
    if (e === 'mcl') this.includeGreeks = false;
  }

  toggleType(t: OptionType, checked: boolean): void {
    this.types = checked ? [...this.types, t] : this.types.filter((x) => x !== t);
  }

  private parseNumbers(text: string): number[] {
    return text
      .split(/[\s,]+/)
      .map((s) => s.trim())
      .filter(Boolean)
      .map(Number)
      .filter((n) => Number.isFinite(n));
  }

  private parseDates(text: string): string[] {
    return text
      .split(/[\s,]+/)
      .map((s) => s.trim())
      .filter(Boolean);
  }

  get canSubmit(): boolean {
    return (
      !!this.workspace() &&
      !!this.engine &&
      this.selectedUnderlyings.length > 0 &&
      this.types.length > 0 &&
      this.parseNumbers(this.strikesText).length > 0 &&
      this.parseDates(this.maturitiesText).length > 0 &&
      !this.running()
    );
  }

  submit(): void {
    const ws = this.workspace();
    if (!ws || !this.engine) return;

    const indicators = ['premium'];
    // per-cell Greeks only when not CPU-mcl and user opted in
    if (this.engine !== 'mcl' && this.includeGreeks) indicators.push(...GREEK_INDICATORS);

    const dto: GridSubmit = {
      workspaceId: ws.id,
      engine: this.engine,
      underlyings: this.selectedUnderlyings,
      types: this.types,
      strikes: this.parseNumbers(this.strikesText),
      maturities: this.parseDates(this.maturitiesText),
      indicators,
      exercise: this.exercise,
    };

    this.error.set(null);
    this.matrices.set([]);
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
          if (!this.matrices().length) {
            this.snack.open('Job done but returned no matrices', 'OK', { duration: 3000 });
          }
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

  ngOnDestroy(): void {
    this.poll?.unsubscribe();
  }

  get progressPct(): number {
    const p = this.progress();
    if (!p || !p.total) return 0;
    return Math.round((p.current / p.total) * 100);
  }
}
