import { Component, Input, OnChanges, computed, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { AgGridAngular } from 'ag-grid-angular';
import type { ColDef, GridApi, GridReadyEvent } from 'ag-grid-community';
import { MarketModel } from './market-model';

interface CurveRow {
  date: string;
  value: number;
}

//! Editable date/value term-structure grid backing one curve-like object (yield_curve,
//! repo_curve, continuous_dividends_curve, discrete_dividends). Edits fold straight back
//! into the object's payload via the shared MarketModel. `valueField` is 'values' for most
//! curves and 'amounts' for discrete dividends.
@Component({
  selector: 'app-curve-grid',
  standalone: true,
  imports: [MatButtonModule, MatIconModule, AgGridAngular],
  template: `
    <div class="curve">
      <ag-grid-angular
        class="ag-theme-quartz grid"
        [rowData]="rows()"
        [columnDefs]="cols()"
        [domLayout]="'autoHeight'"
        [rowSelection]="'single'"
        (gridReady)="onReady($event)"
        (cellValueChanged)="commit()"
      ></ag-grid-angular>
      <div class="curve-actions">
        <button mat-stroked-button (click)="addRow()"><mat-icon>add</mat-icon> Row</button>
        <button mat-stroked-button color="warn" (click)="removeRow()">
          <mat-icon>remove</mat-icon> Row
        </button>
      </div>
    </div>
  `,
  styles: [
    `
      .curve {
        max-width: 460px;
      }
      .grid {
        width: 100%;
      }
      .curve-actions {
        display: flex;
        gap: 8px;
        margin-top: 6px;
      }
    `,
  ],
})
export class CurveGridComponent implements OnChanges {
  @Input({ required: true }) model!: MarketModel;
  @Input({ required: true }) name!: string;
  @Input() valueField: 'values' | 'amounts' = 'values';
  @Input() valueLabel = 'Value';

  private api?: GridApi;
  readonly rows = signal<CurveRow[]>([]);
  readonly cols = computed<ColDef[]>(() => [
    { headerName: 'Date', field: 'date', editable: true, flex: 1, minWidth: 130 },
    {
      headerName: this.valueLabel,
      field: 'value',
      editable: true,
      flex: 1,
      minWidth: 110,
      cellEditor: 'agNumberCellEditor',
      valueParser: (p) => Number(p.newValue),
      type: 'rightAligned',
    },
  ]);

  ngOnChanges(): void {
    const obj = this.model.byName(this.name);
    const dates = (obj?.payload['dates'] as string[]) ?? [];
    const values = (obj?.payload[this.valueField] as number[]) ?? [];
    this.rows.set(dates.map((date, i) => ({ date, value: values[i] ?? 0 })));
  }

  onReady(e: GridReadyEvent): void {
    this.api = e.api;
  }

  addRow(): void {
    const last = this.rows()[this.rows().length - 1];
    this.rows.update((r) => [...r, { date: last?.date ?? '2026-01-01', value: last?.value ?? 0 }]);
    this.commit();
  }

  removeRow(): void {
    const sel = this.api?.getSelectedRows()?.[0] as CurveRow | undefined;
    this.rows.update((r) => (sel ? r.filter((x) => x !== sel) : r.slice(0, -1)));
    this.commit();
  }

  //! read the (possibly edited) grid back and write the two parallel arrays to the object.
  commit(): void {
    const rows: CurveRow[] = [];
    this.api?.forEachNode((n) => rows.push(n.data as CurveRow));
    const live = rows.length ? rows : this.rows();
    this.model.patch(this.name, {
      dates: live.map((r) => r.date),
      [this.valueField]: live.map((r) => Number(r.value)),
    });
  }
}
