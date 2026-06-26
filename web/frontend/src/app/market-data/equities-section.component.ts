import { Component, Input, computed, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatSelectModule } from '@angular/material/select';
import { MatInputModule } from '@angular/material/input';
import { AgGridAngular } from 'ag-grid-angular';
import type {
  CellValueChangedEvent,
  ColDef,
  GridApi,
  GridReadyEvent,
  RowSelectedEvent,
} from 'ag-grid-community';
import { defaultVol, MarketModel, VOL_KINDS, DIV_KINDS } from './market-model';
import { LiveSpotsService } from './live-spots.service';
import type { WsObject } from '../core/models';

interface EquityRow {
  name: string;
  spot: number;
  currency: string;
  vol: string;
  repo: string;
  div: string;
}

//! one row of the combined repo/dividend term structure: a shared pillar date carrying both
//! the repo rate and the dividend value at that pillar (so the two curves stay aligned).
interface TermRow {
  date: string;
  repo: number;
  div: number;
}

//! Equities area: an editable grid (spot + currency) over each !equity, plus a detail
//! panel for the selected stock that edits its referenced volatility (flat/SABR/Heston),
//! repo curve and dividend schedule. Volatility/repo/dividends are sibling objects the
//! equity points at by name; edits here fold into those objects via MarketModel.
@Component({
  selector: 'app-equities-section',
  standalone: true,
  imports: [
    FormsModule,
    MatFormFieldModule,
    MatSelectModule,
    MatInputModule,
    AgGridAngular,
  ],
  templateUrl: './equities-section.component.html',
  styleUrl: './equities-section.component.scss',
})
export class EquitiesSectionComponent {
  @Input({ required: true }) model!: MarketModel;
  @Input() today = '2026-01-01';

  readonly volKinds = VOL_KINDS;
  readonly divKinds = DIV_KINDS;

  private readonly live = inject(LiveSpotsService);
  private api?: GridApi;
  readonly selectedName = signal<string | null>(null);

  //! the Spot column shows the LIVE feed when the feed is on and the symbol is quoted,
  //! otherwise the stored (editable) spot. Reading live.enabled()/live.spots() here makes
  //! the grid re-render on every tick and when the Live toggle flips.
  readonly rows = computed<EquityRow[]>(() => {
    const liveOn = this.live.enabled();
    const quotes = this.live.spots();
    return this.model.equities().map((e) => {
      const vol = this.model.byName(e.payload['volatility'] as string);
      const q = liveOn ? quotes.get(e.name) : undefined;
      return {
        name: e.name,
        spot: q ? q.price : (e.payload['spot'] as number),
        currency: e.payload['currency'] as string,
        vol: vol ? vol.kind.replace('_volatility', '') : '—',
        repo: this.curveSummary(e.payload['repo'] as string),
        div: this.divSummary(e),
      };
    });
  });

  //! true while a live quote is driving this symbol's spot (so it is read-only then).
  private isLive(name: string): boolean {
    return this.live.enabled() && this.live.spots().has(name);
  }

  //! last move direction of a live-quoted symbol (+1 up / -1 down / 0 none), for cell tint.
  private liveDir(name: string): number {
    if (!this.live.enabled()) return 0;
    const q = this.live.spots().get(name);
    return q ? Math.sign(q.price - q.prev) : 0;
  }

  readonly cols = computed<ColDef[]>(() => {
    const currencies = this.model.currencyNames();
    return [
      { headerName: 'Stock', field: 'name', flex: 1, minWidth: 90 },
      {
        headerName: 'Spot',
        field: 'spot',
        //! live feed wins: while a symbol is quoted the cell is read-only and tinted by the
        //! last move; turn Live off (or for an unquoted symbol) and it is editable again.
        editable: (p) => !this.isLive(p.data.name),
        cellEditor: 'agNumberCellEditor',
        valueParser: (p) => Number(p.newValue),
        type: 'rightAligned',
        width: 110,
        cellClassRules: {
          'spot-up': (p) => this.liveDir(p.data.name) > 0,
          'spot-down': (p) => this.liveDir(p.data.name) < 0,
        },
      },
      {
        headerName: 'Ccy',
        field: 'currency',
        editable: true,
        cellEditor: 'agSelectCellEditor',
        cellEditorParams: { values: currencies },
        width: 90,
      },
      { headerName: 'Vol', field: 'vol', width: 90 },
      { headerName: 'Repo', field: 'repo', flex: 1, minWidth: 90 },
      { headerName: 'Dividends', field: 'div', flex: 1, minWidth: 110 },
    ];
  });

  // --- selection-derived state ---
  readonly selected = computed(() => this.model.byName(this.selectedName() ?? '') ?? null);
  readonly volObj = computed(() => {
    const eq = this.selected();
    return eq ? (this.model.byName(eq.payload['volatility'] as string) ?? null) : null;
  });
  readonly repoName = computed(() => this.selected()?.payload['repo'] as string | undefined);
  readonly repoObj = computed(() => this.model.byName(this.repoName() ?? '') ?? null);
  readonly divName = computed(
    () =>
      (this.selected()?.payload['continuous_dividends'] as string | undefined) ??
      (this.selected()?.payload['discrete_dividends'] as string | undefined),
  );
  readonly divObj = computed(() => this.model.byName(this.divName() ?? '') ?? null);

  onReady(e: GridReadyEvent): void {
    this.api = e.api;
  }
  rowId = (p: { data: EquityRow }) => p.data.name;

  onCell(e: CellValueChangedEvent<EquityRow>): void {
    const field = e.colDef.field;
    if (field === 'spot') this.model.patch(e.data.name, { spot: Number(e.data.spot) });
    else if (field === 'currency') this.model.patch(e.data.name, { currency: e.data.currency });
  }

  onRowSelected(e: RowSelectedEvent<EquityRow>): void {
    if (e.node.isSelected() && e.data) this.selectedName.set(e.data.name);
  }

  // --- volatility editing ---
  setVolKind(kind: string): void {
    const vol = this.volObj();
    if (!vol || vol.kind === kind) return;
    const spot = (this.selected()?.payload['spot'] as number) ?? 100;
    this.model.replace(vol.name, defaultVol(kind, spot), kind);
  }
  volNum(field: string): number {
    return (this.volObj()?.payload[field] as number) ?? 0;
  }
  setVolNum(field: string, value: unknown): void {
    const vol = this.volObj();
    if (vol) this.model.patch(vol.name, { [field]: Number(value) });
  }

  // --- dividend kind switching (continuous yield curve <-> discrete cash schedule) ---
  divKind(): string {
    return this.divObj()?.kind ?? 'continuous_dividends_curve';
  }
  divValueField(): 'values' | 'amounts' {
    return this.divKind() === 'discrete_dividends' ? 'amounts' : 'values';
  }
  setDivKind(kind: string): void {
    const eq = this.selected();
    const div = this.divObj();
    if (!eq || !div || div.kind === kind) return;
    // re-point the equity to the right field and convert the schedule's payload
    const field = kind === 'discrete_dividends' ? 'amounts' : 'values';
    const dates = (div.payload['dates'] as string[]) ?? [this.today];
    const old = (div.payload['values'] ?? div.payload['amounts'] ?? []) as number[];
    this.model.replace(div.name, { dates, [field]: old }, kind);
    this.model.patch(eq.name, {
      continuous_dividends: kind === 'continuous_dividends_curve' ? div.name : undefined,
      discrete_dividends: kind === 'discrete_dividends' ? div.name : undefined,
    });
  }

  // --- SABR pillar grid (one row per maturity; 5 parallel arrays) ---
  private sabrApi?: GridApi;
  readonly sabrCols: ColDef[] = ['maturities', 'alpha', 'beta', 'rho', 'nu'].map((f) => ({
    headerName: f === 'maturities' ? 'maturity' : f,
    field: f,
    editable: true,
    cellEditor: 'agNumberCellEditor',
    valueParser: (p) => Number(p.newValue),
    flex: 1,
    minWidth: 90,
    type: 'rightAligned',
  }));
  readonly sabrRows = computed<Record<string, number>[]>(() => {
    const p = this.volObj()?.payload ?? {};
    const mats = (p['maturities'] as number[]) ?? [];
    return mats.map((_, i) => ({
      maturities: mats[i],
      alpha: (p['alpha'] as number[])?.[i],
      beta: (p['beta'] as number[])?.[i],
      rho: (p['rho'] as number[])?.[i],
      nu: (p['nu'] as number[])?.[i],
    }));
  });
  onSabrReady(e: GridReadyEvent): void {
    this.sabrApi = e.api;
  }
  commitSabr(): void {
    const vol = this.volObj();
    if (!vol) return;
    const rows: Record<string, number>[] = [];
    this.sabrApi?.forEachNode((n) => rows.push(n.data as Record<string, number>));
    const live = rows.length ? rows : this.sabrRows();
    const col = (f: string) => live.map((r) => Number(r[f]));
    this.model.replace(
      vol.name,
      { maturities: col('maturities'), alpha: col('alpha'), beta: col('beta'), rho: col('rho'), nu: col('nu') },
      'sabr_volatility',
    );
  }

  // --- Heston pillar grid: same tabular display as SABR, but a single row of scalar params.
  // Editing a cell patches only that field, so the Bates jump_* fields stay absent from the
  // payload (pure Heston) until the user actually sets them.
  readonly hestonFields = [
    'spot',
    'init_vol',
    'long_vol',
    'kappa',
    'vol_of_vol',
    'jump_intensity',
    'jump_mean',
    'jump_vol',
  ];
  readonly hestonCols: ColDef[] = this.hestonFields.map((f) => ({
    headerName: f,
    field: f,
    editable: true,
    cellEditor: 'agNumberCellEditor',
    valueParser: (p) => Number(p.newValue),
    flex: 1,
    minWidth: 80,
    type: 'rightAligned',
  }));
  readonly hestonRows = computed<Record<string, number>[]>(() => {
    const p = this.volObj()?.payload ?? {};
    const row: Record<string, number> = {};
    for (const f of this.hestonFields) row[f] = (p[f] as number) ?? 0;
    return [row];
  });
  commitHeston(e: CellValueChangedEvent<Record<string, number>>): void {
    const field = e.colDef.field;
    if (field) this.setVolNum(field, e.data[field]);
  }

  // --- combined repo + dividend term structure (one shared pillar table) ---
  // Repo and dividends share the same pillar dates and are edited in a single grid; every
  // commit rewrites BOTH curves with the same `dates`, so they always have an equal pillar
  // count. The dividend column reads/writes 'values' (continuous yield) or 'amounts' (cash).
  private termApi?: GridApi;
  readonly termDates = computed<string[]>(() => {
    const rd = (this.repoObj()?.payload['dates'] as string[]) ?? [];
    const dd = (this.divObj()?.payload['dates'] as string[]) ?? [];
    return rd.length >= dd.length ? rd : dd;
  });
  readonly termRows = computed<TermRow[]>(() => {
    const dates = this.termDates();
    const rv = (this.repoObj()?.payload['values'] as number[]) ?? [];
    const dv = (this.divObj()?.payload[this.divValueField()] as number[]) ?? [];
    const lastR = rv[rv.length - 1] ?? 0;
    const lastD = dv[dv.length - 1] ?? 0;
    return dates.map((date, i) => ({ date, repo: rv[i] ?? lastR, div: dv[i] ?? lastD }));
  });
  readonly termCols = computed<ColDef[]>(() => [
    { headerName: 'Pillar', field: 'date', editable: true, flex: 1, minWidth: 130 },
    {
      headerName: 'Repo (%)',
      field: 'repo',
      editable: true,
      cellEditor: 'agNumberCellEditor',
      valueParser: (p) => Number(p.newValue),
      flex: 1,
      minWidth: 100,
      type: 'rightAligned',
    },
    {
      headerName: this.divValueField() === 'amounts' ? 'Dividend (cash)' : 'Dividend (yield %)',
      field: 'div',
      editable: true,
      cellEditor: 'agNumberCellEditor',
      valueParser: (p) => Number(p.newValue),
      flex: 1,
      minWidth: 120,
      type: 'rightAligned',
    },
  ]);
  onTermReady(e: GridReadyEvent): void {
    this.termApi = e.api;
  }
  commitTerm(): void {
    const rows: TermRow[] = [];
    this.termApi?.forEachNode((n) => rows.push(n.data as TermRow));
    const live = rows.length ? rows : this.termRows();
    const dates = live.map((r) => r.date);
    const repo = this.repoName();
    if (repo) this.model.patch(repo, { dates, values: live.map((r) => Number(r.repo)) });
    const div = this.divObj();
    if (div) this.model.patch(div.name, { dates, [this.divValueField()]: live.map((r) => Number(r.div)) });
  }

  private curveSummary(name?: string): string {
    const obj = name ? this.model.byName(name) : undefined;
    const v = obj?.payload['values'] as number[] | undefined;
    return v?.length ? `${v[0]}%` : '—';
  }
  private divSummary(eq: WsObject): string {
    const div = this.model.byName(
      (eq.payload['continuous_dividends'] ?? eq.payload['discrete_dividends']) as string,
    );
    if (!div) return '—';
    if (div.kind === 'discrete_dividends') {
      const a = div.payload['amounts'] as number[] | undefined;
      return a?.length ? `${a.length} cash` : 'cash';
    }
    const v = div.payload['values'] as number[] | undefined;
    return v?.length ? `${v[0]}% yield` : 'yield';
  }
}
