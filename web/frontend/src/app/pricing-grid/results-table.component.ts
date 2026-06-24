import { Component, Input, OnChanges, ViewChild, computed, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { AgGridAngular } from 'ag-grid-angular';
import type { ColDef, GridApi, GridReadyEvent, ValueFormatterParams } from 'ag-grid-community';
import { GridMatrix } from '../core/models';

const GREEKS = ['delta', 'gamma', 'vega', 'rho', 'theta'];

interface ResRow {
  maturity: string;
  strike: number;
  premium: number;
  [greek: string]: number | string;
}

//! One (underlying, type) result as a flat table: one row per (date, strike) computation,
//! with premium + every available Greek as COLUMNS (rows ordered by date, then strike).
//! The contract currency is shown in the title and on the premium column.
@Component({
  selector: 'app-results-table',
  standalone: true,
  imports: [MatButtonModule, MatIconModule, AgGridAngular],
  template: `
    <div class="rt-head">
      <span class="rt-title">{{ matrix.underlying }} · {{ matrix.type }} · {{ matrix.currency }}</span>
      <span class="thoth-spacer"></span>
      <button mat-stroked-button (click)="exportCsv()"><mat-icon>download</mat-icon> CSV</button>
    </div>
    <ag-grid-angular
      class="ag-theme-quartz grid"
      [rowData]="rows()"
      [columnDefs]="cols()"
      [domLayout]="'autoHeight'"
      (gridReady)="onReady($event)"
    ></ag-grid-angular>
  `,
  styles: [
    `
      :host {
        display: block;
        margin-bottom: 20px;
      }
      .rt-head {
        display: flex;
        align-items: center;
        margin-bottom: 6px;
      }
      .rt-title {
        font-weight: 600;
      }
      .thoth-spacer {
        flex: 1;
      }
      .grid {
        width: 100%;
      }
    `,
  ],
})
export class ResultsTableComponent implements OnChanges {
  @Input({ required: true }) matrix!: GridMatrix;
  @ViewChild(AgGridAngular) gridCmp?: AgGridAngular;
  private api?: GridApi;

  readonly rows = signal<ResRow[]>([]);
  readonly greekCols = signal<string[]>([]);

  readonly cols = computed<ColDef[]>(() => [
    { headerName: 'Date', field: 'maturity', pinned: 'left', width: 130, sort: 'asc' },
    { headerName: 'Strike', field: 'strike', width: 100, type: 'rightAligned', valueFormatter: fmt },
    {
      headerName: `Premium (${this.matrix.currency})`,
      field: 'premium',
      width: 150,
      type: 'rightAligned',
      cellStyle: { fontWeight: '600' },
      valueFormatter: fmt,
    },
    ...this.greekCols().map<ColDef>((g) => ({
      headerName: g,
      field: g,
      flex: 1,
      minWidth: 90,
      type: 'rightAligned',
      valueFormatter: fmt,
    })),
  ]);

  ngOnChanges(): void {
    const m = this.matrix;
    const present = GREEKS.filter((g) => m.greeks?.[g]);
    this.greekCols.set(present);
    const rows: ResRow[] = [];
    // iterate dates (outer) then strikes (inner) -> rows read "per date"
    m.maturities.forEach((mat, j) => {
      m.strikes.forEach((strike, i) => {
        const row: ResRow = { maturity: mat, strike, premium: m.premium[i]?.[j] };
        for (const g of present) row[g] = m.greeks[g]![i]?.[j];
        rows.push(row);
      });
    });
    this.rows.set(rows);
  }

  onReady(e: GridReadyEvent): void {
    this.api = e.api;
  }

  exportCsv(): void {
    this.api?.exportDataAsCsv({ fileName: `${this.matrix.underlying}_${this.matrix.type}.csv` });
  }
}

function fmt(p: ValueFormatterParams): string {
  const v = p.value;
  if (v == null || !Number.isFinite(v as number)) return '';
  return (v as number).toLocaleString(undefined, { minimumFractionDigits: 4, maximumFractionDigits: 4 });
}
