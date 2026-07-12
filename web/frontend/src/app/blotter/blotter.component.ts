import { Component, OnInit, computed, effect, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatCardModule } from '@angular/material/card';
import { MatTableModule } from '@angular/material/table';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatTooltipModule } from '@angular/material/tooltip';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MatSnackBar } from '@angular/material/snack-bar';
import { PanelContextService } from '../panels/panel-context.service';
import { LiveSpotsService } from '../market-data/live-spots.service';
import { BlotterService, BlotterRow } from './blotter.service';

//! Global monitoring blotter: every product sent from a pricing panel becomes a live row.
//! Live mode re-prices the whole book off the feed on a throttle, tinting each premium green/
//! red on its last move (same charte as the option chain). Rows can be removed or cleared.
@Component({
  selector: 'app-blotter',
  standalone: true,
  imports: [
    FormsModule,
    MatCardModule,
    MatTableModule,
    MatButtonModule,
    MatIconModule,
    MatFormFieldModule,
    MatInputModule,
    MatTooltipModule,
    MatCheckboxModule,
  ],
  templateUrl: './blotter.component.html',
  styleUrl: './blotter.component.scss',
})
export class BlotterComponent implements OnInit {
  readonly b = inject(BlotterService);
  private readonly ctx = inject(PanelContextService);
  private readonly live = inject(LiveSpotsService);
  private readonly snack = inject(MatSnackBar);

  readonly busy = signal(false);

  //! the tick column first, fixed columns + whichever Greeks appear, then actions.
  readonly columns = computed<string[]>(() => [
    'select',
    'label',
    'kind',
    'underlying',
    'spot',
    'engine',
    'premium',
    'ccy',
    ...this.b.greekColumns(),
    'status',
    'actions',
  ]);

  constructor() {
    // first-launch demo book: once the workspace's underlyings have loaded, if the
    // blotter has never been seeded (and is empty), populate it with 10 random
    // contracts so a fresh install lands on a live monitoring book, not a blank tab.
    // The effect fires whenever the objects signal changes; seedDemo is idempotent
    // (guarded by a localStorage flag), so later object refreshes are no-ops.
    // allowSignalWrites: seedDemo writes the rows signal (adds the demo rows) — the
    // one legitimate signal write from this effect, in response to the loaded book.
    effect(
      () => {
        const underlyings = this.ctx.underlyingObjects().map((o) => o.name);
        const ws = this.ctx.workspace();
        if (!underlyings.length || !ws) return;
        this.b.seedDemo(ws.id, underlyings, this.ctx.defaultCurrency(), ws.today);
      },
      { allowSignalWrites: true },
    );
  }

  ngOnInit(): void {
    // ensure the shared workspace/objects are loaded (the blotter may be the first tab opened).
    this.ctx.init();
  }

  underlyingOf(row: BlotterRow): string {
    return (row.request.instrument['underlying'] as string) ?? '';
  }

  //! live spot for the row's underlying (keyed by ticker, same as the equities grid), or
  //! undefined when Live is off or the symbol isn't streamed.
  spotOf(row: BlotterRow): number | undefined {
    if (!this.live.enabled()) return undefined;
    return this.live.spots().get(this.underlyingOf(row))?.price;
  }

  //! +1/-1 on the last spot move, for the green/red tint (mirrors the equities grid).
  spotDir(row: BlotterRow): number {
    const q = this.live.spots().get(this.underlyingOf(row));
    return q ? Math.sign(q.price - q.prev) : 0;
  }

  greekOf(row: BlotterRow, g: string): number | undefined {
    return (row.greeks as Record<string, number | undefined>)[g];
  }

  fmt(v: number | null | undefined): string {
    if (v == null || !Number.isFinite(v)) return '—';
    return v.toLocaleString(undefined, { minimumFractionDigits: 4, maximumFractionDigits: 4 });
  }

  //! a word describing what an action targets (for the button label / snackbar):
  //! the ticked rows, or the whole book when nothing is ticked.
  targetLabel(): string {
    const n = this.b.selectedCount();
    return n ? `${n} selected` : 'all';
  }

  //! re-price the targeted rows (ticked, or all).
  reprice(): void {
    void this.b.repriceSelected(false);
  }

  //! render + download the targeted rows' termsheets as one Markdown file.
  async termsheet(): Promise<void> {
    this.busy.set(true);
    try {
      const { ok, failed } = await this.b.downloadTermsheets();
      const msg = failed
        ? `${ok} termsheet(s) downloaded, ${failed} failed`
        : `${ok} termsheet(s) downloaded`;
      this.snack.open(msg, 'OK', { duration: 3000 });
    } finally {
      this.busy.set(false);
    }
  }
}
