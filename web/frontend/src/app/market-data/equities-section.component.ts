import { Component, Input, computed, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
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
import { CurveGridComponent } from './curve-grid.component';
import { defaultVol, MarketModel, VOL_KINDS, DIV_KINDS } from './market-model';
import type { WsObject } from '../core/models';

interface EquityRow {
  name: string;
  spot: number;
  currency: string;
  vol: string;
  repo: string;
  div: string;
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
    MatButtonModule,
    MatIconModule,
    MatFormFieldModule,
    MatSelectModule,
    MatInputModule,
    AgGridAngular,
    CurveGridComponent,
  ],
  templateUrl: './equities-section.component.html',
  styleUrl: './equities-section.component.scss',
})
export class EquitiesSectionComponent {
  @Input({ required: true }) model!: MarketModel;
  @Input() today = '2026-01-01';

  readonly volKinds = VOL_KINDS;
  readonly divKinds = DIV_KINDS;

  private api?: GridApi;
  readonly selectedName = signal<string | null>(null);

  readonly rows = computed<EquityRow[]>(() =>
    this.model.equities().map((e) => {
      const vol = this.model.byName(e.payload['volatility'] as string);
      return {
        name: e.name,
        spot: e.payload['spot'] as number,
        currency: e.payload['currency'] as string,
        vol: vol ? vol.kind.replace('_volatility', '') : '—',
        repo: this.curveSummary(e.payload['repo'] as string),
        div: this.divSummary(e),
      };
    }),
  );

  readonly cols = computed<ColDef[]>(() => {
    const currencies = this.model.currencyNames();
    return [
      { headerName: 'Stock', field: 'name', flex: 1, minWidth: 90 },
      {
        headerName: 'Spot',
        field: 'spot',
        editable: true,
        cellEditor: 'agNumberCellEditor',
        valueParser: (p) => Number(p.newValue),
        type: 'rightAligned',
        width: 110,
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

  addEquity(): void {
    this.selectedName.set(this.model.addEquity(this.today));
  }

  removeEquity(): void {
    const eq = this.selected();
    if (!eq) return;
    const owned = [
      eq.payload['volatility'],
      eq.payload['repo'],
      eq.payload['continuous_dividends'],
      eq.payload['discrete_dividends'],
    ].filter((n): n is string => typeof n === 'string');
    this.model.remove(eq.name, ...owned);
    this.selectedName.set(null);
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
  addPillar(): void {
    const vol = this.volObj();
    if (!vol) return;
    const push = (f: string, d: number) => [...((vol.payload[f] as number[]) ?? []), d];
    this.model.patch(vol.name, {
      maturities: push('maturities', 1),
      alpha: push('alpha', 0.2),
      beta: push('beta', 1),
      rho: push('rho', 0),
      nu: push('nu', 0.4),
    });
  }
  removePillar(): void {
    const vol = this.volObj();
    if (!vol) return;
    const pop = (f: string) => ((vol.payload[f] as number[]) ?? []).slice(0, -1);
    this.model.patch(vol.name, {
      maturities: pop('maturities'),
      alpha: pop('alpha'),
      beta: pop('beta'),
      rho: pop('rho'),
      nu: pop('nu'),
    });
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
