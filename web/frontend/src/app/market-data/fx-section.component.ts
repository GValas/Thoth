import { Component, Input, computed, inject } from '@angular/core';
import { AgGridAngular } from 'ag-grid-angular';
import type {
  CellClassParams,
  CellValueChangedEvent,
  ColDef,
  ValueFormatterParams,
} from 'ag-grid-community';
import { defaultVol, MarketModel } from './market-model';
import { LiveSpotsService } from './live-spots.service';

interface FxRow {
  name: string;
  base_currency: string;
  underlying_currency: string;
  spot: number;
  vol: number;
  //! true for a triangulated cross (e.g. eur/jpy): derived from the pivot legs, not editable.
  induced: boolean;
}

//! FX area. Pivot pairs (usd/eur, usd/jpy) are DERIVED from the currencies (one !forex per
//! non-pivot currency, sharing usd as underlying — see MarketModel.syncForex) and carry an
//! editable spot + flat vol. The cross pairs (eur/jpy) are INDUCED by triangulation from the
//! two pivot legs so they stay arbitrage-free; they are display-only and not persisted (the
//! engine's correlation triangle only accepts the pivot basis). When Live is on, pivot spots
//! come from the feed (read-only, green/red on the last move) and the cross recomputes off
//! the live legs.
@Component({
  selector: 'app-fx-section',
  standalone: true,
  imports: [AgGridAngular],
  template: `
    @if (rows().length) {
      <ag-grid-angular
        class="ag-theme-quartz grid"
        [rowData]="rows()"
        [columnDefs]="cols"
        [domLayout]="'autoHeight'"
        [getRowId]="rowId"
        (cellValueChanged)="onCell($event)"
      ></ag-grid-angular>
      <p class="hint">
        Pivot pairs follow the currencies · cross pairs are induced (triangulated) · spot in base
        currency · vol in %
      </p>
    } @else {
      <p class="empty">Generate the market data to populate the fx pairs.</p>
    }
  `,
  styles: [
    `
      :host {
        display: block;
      }
      .hint {
        color: var(--thoth-text-muted, #888);
        font-size: 12px;
      }
      .grid {
        width: 100%;
        max-width: 760px;
      }
      .empty {
        color: var(--thoth-text-muted, #888);
      }
      :host ::ng-deep .fx-up {
        color: var(--thoth-positive);
      }
      :host ::ng-deep .fx-down {
        color: var(--thoth-negative);
      }
      :host ::ng-deep .fx-induced {
        font-style: italic;
        color: var(--thoth-text-muted, #777);
      }
    `,
  ],
})
export class FxSectionComponent {
  @Input({ required: true }) model!: MarketModel;

  private readonly live = inject(LiveSpotsService);

  //! current spot of a pivot pair: the live quote when the feed is on and the pair is quoted,
  //! otherwise the stored (editable) spot.
  private pivotSpot(name: string): number {
    if (this.live.enabled()) {
      const q = this.live.spots().get(name);
      if (q) return q.price;
    }
    return (this.model.byName(name)?.payload['spot'] as number) ?? 0;
  }

  readonly rows = computed<FxRow[]>(() => {
    const pivots = this.model.forexs();
    const rows: FxRow[] = pivots.map((f) => ({
      name: f.name,
      base_currency: f.payload['base_currency'] as string,
      underlying_currency: f.payload['underlying_currency'] as string,
      spot: this.pivotSpot(f.name),
      vol: (this.model.byName(f.payload['volatility'] as string)?.payload['volatility'] as number) ?? 0,
      induced: false,
    }));

    // induced crosses: for every pair of pivot legs (pivot/x, pivot/y) emit x/y = leg_y / leg_x.
    for (let a = 0; a < pivots.length; a++) {
      for (let b = 0; b < pivots.length; b++) {
        if (a === b) continue;
        const x = pivots[a].payload['base_currency'] as string;
        const y = pivots[b].payload['base_currency'] as string;
        if (x >= y) continue; // one direction only (x/y)
        const sx = this.pivotSpot(pivots[a].name);
        const sy = this.pivotSpot(pivots[b].name);
        rows.push({
          name: `${x}/${y}`,
          underlying_currency: x,
          base_currency: y,
          spot: sx ? sy / sx : 0,
          vol: NaN,
          induced: true,
        });
      }
    }
    return rows;
  });

  //! +1/-1 on a pivot pair's last live move (induced rows aren't tracked → 0).
  private dir(name: string): number {
    if (!this.live.enabled()) return 0;
    const q = this.live.spots().get(name);
    return q ? Math.sign(q.price - q.prev) : 0;
  }

  //! a pivot spot is read-only while a live quote is driving it (same as the equities grid).
  private liveQuoted(name: string): boolean {
    return this.live.enabled() && this.live.spots().has(name);
  }

  readonly cols: ColDef<FxRow>[] = [
    {
      headerName: 'Pair',
      field: 'name',
      flex: 1,
      minWidth: 100,
      cellClassRules: { 'fx-induced': (p) => !!p.data?.induced },
    },
    { headerName: 'Underlying', field: 'underlying_currency', width: 120 },
    { headerName: 'Base', field: 'base_currency', width: 110 },
    {
      headerName: 'Spot',
      field: 'spot',
      width: 140,
      editable: (p) => !!p.data && !p.data.induced && !this.liveQuoted(p.data.name),
      cellEditor: 'agNumberCellEditor',
      valueParser: (p) => Number(p.newValue),
      type: 'rightAligned',
      valueFormatter: (p: ValueFormatterParams) =>
        Number.isFinite(p.value) ? (p.value as number).toLocaleString(undefined, { maximumFractionDigits: 4 }) : '',
      cellClassRules: {
        'fx-up': (p: CellClassParams<FxRow>) => !!p.data && this.dir(p.data.name) > 0,
        'fx-down': (p: CellClassParams<FxRow>) => !!p.data && this.dir(p.data.name) < 0,
      },
    },
    {
      headerName: 'Vol (%)',
      field: 'vol',
      width: 120,
      editable: (p) => !!p.data && !p.data.induced,
      cellEditor: 'agNumberCellEditor',
      valueParser: (p) => Number(p.newValue),
      type: 'rightAligned',
      valueFormatter: (p: ValueFormatterParams) => (Number.isFinite(p.value) ? String(p.value) : '—'),
    },
  ];

  rowId = (p: { data: FxRow }) => p.data.name;

  onCell(e: CellValueChangedEvent<FxRow>): void {
    if (e.data.induced) return; // derived row — nothing to persist
    const fx = this.model.byName(e.data.name);
    if (!fx) return;
    if (e.colDef.field === 'vol') {
      const volName = (fx.payload['volatility'] as string) || `${fx.name}_vol`;
      if (!this.model.has(volName)) {
        this.model.upsert({ name: volName, kind: 'bs_volatility', payload: defaultVol('bs_volatility') });
        this.model.patch(fx.name, { volatility: volName });
      }
      this.model.patch(volName, { volatility: Number(e.data.vol) });
    } else if (e.colDef.field === 'spot') {
      this.model.patch(fx.name, { spot: Number(e.data.spot) });
    }
  }
}
