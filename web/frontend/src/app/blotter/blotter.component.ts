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
import { CdkDragDrop, DragDropModule, moveItemInArray } from '@angular/cdk/drag-drop';
import { Router } from '@angular/router';
import { PanelContextService } from '../panels/panel-context.service';
import { PanelPrefillService } from '../panels/panel-prefill.service';
import { LiveSpotsService } from '../market-data/live-spots.service';
import { BlotterService, BlotterRow, BlotterStatus } from './blotter.service';
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
    DragDropModule,
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
    if (col === 'status') return this.statusLabel(row.status);
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

  //! Greek columns are hidden by default (a lighter monitoring view); the toolbar toggles them.
  readonly showGreeks = signal(false);
  private static readonly GREEKS = ['delta', 'gamma', 'vega', 'rho', 'theta'];

  //! user-reorderable order of the DATA columns (drag a header). `select` stays first and
  //! `actions` last; Greeks keep their slot here even while hidden, so toggling them back on
  //! restores their place.
  readonly colOrder = signal<string[]>([
    'kind', 'label', 'underlying', 'spot', 'engine', 'premium', 'ccy',
    'delta', 'gamma', 'vega', 'rho', 'theta', 'priced', 'status',
  ]);

  //! the displayed columns: select + the ordered data columns (Greeks only when shown and
  //! actually present in the book) + actions.
  readonly columns = computed<string[]>(() => {
    const present = new Set(this.b.greekColumns());
    const show = this.showGreeks();
    const mid = this.colOrder().filter((c) =>
      BlotterComponent.GREEKS.includes(c) ? show && present.has(c) : true,
    );
    return ['select', ...mid, 'actions'];
  });

  //! whether a column is a (draggable, sortable, filterable) data column vs a fixed one.
  isDataColumn(col: string): boolean {
    return col !== 'select' && col !== 'actions';
  }

  //! show / hide the Greek columns.
  toggleGreeks(): void {
    this.showGreeks.update((v) => !v);
  }

  //! reorder a data column by dragging its header. Move by NAME so the visible/hidden Greek
  //! split never mismatches the drop indices; the fixed select/actions never move.
  dropColumn(e: CdkDragDrop<unknown>): void {
    const displayed = this.columns();
    const from = displayed[e.previousIndex];
    const to = displayed[e.currentIndex];
    if (!from || !to || !this.isDataColumn(from) || !this.isDataColumn(to)) return;
    const order = [...this.colOrder()];
    const fi = order.indexOf(from);
    const ti = order.indexOf(to);
    if (fi >= 0 && ti >= 0) {
      moveItemInArray(order, fi, ti);
      this.colOrder.set(order);
    }
  }

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
    this.prefill.request(row.kind, row.request.engine, row.request.instrument, row.id);
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
    return v.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 });
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

  //! --- sales workflow status column: label / icon / tooltip per state ---------
  private static readonly STATUS_META: Record<BlotterStatus, { label: string; icon: string; tip: string }> = {
    new: { label: 'New', icon: 'fiber_new', tip: 'Just created — not yet priced' },
    quoting: { label: 'Quoting', icon: 'hourglass_top', tip: 'Being priced by the engine' },
    quoted: { label: 'Quoted', icon: 'request_quote', tip: 'Priced — awaiting the sales decision' },
    error: { label: 'Error', icon: 'error', tip: 'Pricing failed' },
    traded: { label: 'Traded', icon: 'check_circle', tip: 'Dealt on behalf of the client' },
    missed: { label: 'Missed', icon: 'cancel', tip: 'Client dealt elsewhere' },
  };
  statusLabel(s: BlotterStatus): string {
    return BlotterComponent.STATUS_META[s]?.label ?? s;
  }
  statusIcon(s: BlotterStatus): string {
    return BlotterComponent.STATUS_META[s]?.icon ?? 'help';
  }
  statusTooltip(s: BlotterStatus): string {
    return BlotterComponent.STATUS_META[s]?.tip ?? '';
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
