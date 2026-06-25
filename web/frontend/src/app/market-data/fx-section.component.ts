import { Component, Input, computed } from '@angular/core';
import { AgGridAngular } from 'ag-grid-angular';
import type { CellValueChangedEvent, ColDef } from 'ag-grid-community';
import { defaultVol, MarketModel } from './market-model';

interface FxRow {
  name: string;
  base_currency: string;
  underlying_currency: string;
  spot: number;
  vol: number;
}

//! FX area: pairs are DERIVED from the currencies (one !forex per non-base currency, vs the
//! base) — see MarketModel.syncForex — so there is no add/remove here. Only spot and the
//! pair's flat vol (a sibling !bs_volatility named `<pair>_vol`) are editable.
@Component({
  selector: 'app-fx-section',
  standalone: true,
  imports: [AgGridAngular],
  template: `
    @if (model.forexs().length) {
      <ag-grid-angular
        class="ag-theme-quartz grid"
        [rowData]="rows()"
        [columnDefs]="cols"
        [domLayout]="'autoHeight'"
        [getRowId]="rowId"
        (cellValueChanged)="onCell($event)"
      ></ag-grid-angular>
      <p class="hint">Pairs follow the currencies (underlying/base) · spot in base currency · vol in %</p>
    } @else {
      <p class="empty">Add a second currency to create an fx pair automatically.</p>
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
        max-width: 720px;
      }
      .empty {
        color: var(--thoth-text-muted, #888);
      }
    `,
  ],
})
export class FxSectionComponent {
  @Input({ required: true }) model!: MarketModel;

  readonly rows = computed<FxRow[]>(() =>
    this.model.forexs().map((f) => ({
      name: f.name,
      base_currency: f.payload['base_currency'] as string,
      underlying_currency: f.payload['underlying_currency'] as string,
      spot: f.payload['spot'] as number,
      vol: (this.model.byName(f.payload['volatility'] as string)?.payload['volatility'] as number) ?? 0,
    })),
  );

  readonly cols: ColDef[] = [
    { headerName: 'Pair', field: 'name', flex: 1, minWidth: 100 },
    { headerName: 'Underlying', field: 'underlying_currency', width: 120 },
    { headerName: 'Base', field: 'base_currency', width: 110 },
    {
      headerName: 'Spot',
      field: 'spot',
      width: 130,
      editable: true,
      cellEditor: 'agNumberCellEditor',
      valueParser: (p) => Number(p.newValue),
      type: 'rightAligned',
    },
    {
      headerName: 'Vol (%)',
      field: 'vol',
      width: 120,
      editable: true,
      cellEditor: 'agNumberCellEditor',
      valueParser: (p) => Number(p.newValue),
      type: 'rightAligned',
    },
  ];

  rowId = (p: { data: FxRow }) => p.data.name;

  onCell(e: CellValueChangedEvent<FxRow>): void {
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
