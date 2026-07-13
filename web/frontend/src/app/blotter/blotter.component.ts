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
import { Router } from '@angular/router';
import { PanelContextService } from '../panels/panel-context.service';
import { PanelPrefillService } from '../panels/panel-prefill.service';
import { LiveSpotsService } from '../market-data/live-spots.service';
import { BlotterService, BlotterRow } from './blotter.service';
import { BlotterColHeaderComponent, SortDir } from './blotter-col-header.component';

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
    BlotterColHeaderComponent,
  ],
  templateUrl: './blotter.component.html',
  styleUrl: './blotter.component.scss',
})
export class BlotterComponent implements OnInit {
  readonly b = inject(BlotterService);
  private readonly ctx = inject(PanelContextService);
  private readonly live = inject(LiveSpotsService);
  private readonly snack = inject(MatSnackBar);
  private readonly router = inject(Router);
  private readonly prefill = inject(PanelPrefillService);

  //! id of the row whose termsheet is currently being generated (for the per-row
  //! button spinner), or null when idle.
  readonly termsheetBusyId = signal<string | null>(null);

  //! sort + per-column text filter state (any data column). The header component drives them.
  readonly sortCol = signal<string | null>(null);
  readonly sortAsc = signal(true);
  readonly filters = signal<Record<string, string>>({});

  //! click a header: cycle this column asc -> desc -> unsorted; a new column starts asc.
  toggleSort(col: string): void {
    if (this.sortCol() !== col) {
      this.sortCol.set(col);
      this.sortAsc.set(true);
    } else if (this.sortAsc()) {
      this.sortAsc.set(false);
    } else {
      this.sortCol.set(null); //!< third click clears the sort
    }
  }
  sortDirFor(col: string): SortDir {
    return this.sortCol() === col ? (this.sortAsc() ? 'asc' : 'desc') : null;
  }
  filterFor(col: string): string {
    return this.filters()[col] ?? '';
  }
  setFilter(col: string, value: string): void {
    this.filters.update((f) => ({ ...f, [col]: value }));
  }

  //! comparable value for sorting a (row, column) — a number where the cell is numeric.
  private sortValue(row: BlotterRow, col: string): string | number | undefined {
    switch (col) {
      case 'kind':
        return row.kind;
      case 'label':
        return row.label;
      case 'underlying':
        return this.underlyingOf(row);
      case 'spot':
        return this.spotOf(row);
      case 'engine':
        return row.request.engine;
      case 'premium':
        return row.premium ?? undefined;
      case 'ccy':
        return row.currency;
      case 'priced':
        return row.pricedAt ? row.pricedAt.getTime() : undefined;
      case 'status':
        return row.status;
      default:
        return this.greekOf(row, col); //!< a Greek column
    }
  }

  //! the text a column filter matches against — the same string the cell displays.
  private filterText(row: BlotterRow, col: string): string {
    if (col === 'spot') return this.fmt(this.spotOf(row));
    if (col === 'premium') return this.fmt(row.premium);
    if (col === 'priced') return this.fmtTime(row.pricedAt);
    const v = this.sortValue(row, col);
    if (v == null) return '';
    return typeof v === 'number' ? this.fmt(v) : String(v);
  }

  //! rows after the per-column text filters + an optional single-column sort. Reads the live
  //! spots (via sortValue/filterText) so it re-orders as the feed re-prices.
  readonly displayRows = computed<BlotterRow[]>(() => {
    const rows = this.b.rows();
    const active = Object.entries(this.filters()).filter(([, q]) => q.trim() !== '');
    let out = active.length
      ? rows.filter((r) =>
          active.every(([col, q]) => this.filterText(r, col).toLowerCase().includes(q.trim().toLowerCase())),
        )
      : rows;

    const col = this.sortCol();
    if (col) {
      const dir = this.sortAsc() ? 1 : -1;
      out = [...out].sort((a, b) => {
        const va = this.sortValue(a, col);
        const vb = this.sortValue(b, col);
        if (va == null && vb == null) return 0;
        if (va == null) return 1; //!< empty cells sort last, both directions
        if (vb == null) return -1;
        const c =
          typeof va === 'number' && typeof vb === 'number'
            ? va - vb
            : String(va).localeCompare(String(vb));
        return c * dir;
      });
    }
    return out;
  });

  //! the tick column first, fixed columns + whichever Greeks appear, then actions.
  readonly columns = computed<string[]>(() => [
    'select',
    'kind',
    'label',
    'underlying',
    'spot',
    'engine',
    'premium',
    'ccy',
    ...this.b.greekColumns(),
    'priced',
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
        // equities only: every (kind, engine) combo the seed generates needs a griddable
        // underlying (ANA barriers/varswaps) with a diffusable vol — FX pairs trip those.
        // Carry the spot so barriers book absolute cash levels (engine levels are absolute).
        const underlyings = this.ctx
          .underlyingObjects()
          .filter((o) => o.kind === 'equity')
          .map((o) => ({ name: o.name, spot: Number(o.payload['spot']) || 100 }));
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

  //! double-click a row -> open its contract in the matching pricing panel, prefilled.
  openInPanel(row: BlotterRow): void {
    this.prefill.request(row.kind, row.request.engine, row.request.instrument);
    void this.router.navigate(['/panels']);
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

  //! wall-clock time (HH:MM:SS) of a row's last successful pricing, or a dash.
  fmtTime(d: Date | null): string {
    if (!d) return '—';
    return d.toLocaleTimeString(undefined, {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      hour12: false,
    });
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

  //! render + download ONE row's termsheet as its own Markdown file. Tracks the
  //! in-flight row id so the button can show a spinner and not be double-clicked.
  async rowTermsheet(row: BlotterRow): Promise<void> {
    this.termsheetBusyId.set(row.id);
    try {
      const ok = await this.b.downloadRowTermsheet(row);
      if (!ok) this.snack.open('Termsheet generation failed', 'OK', { duration: 3000 });
    } finally {
      this.termsheetBusyId.set(null);
    }
  }
}
