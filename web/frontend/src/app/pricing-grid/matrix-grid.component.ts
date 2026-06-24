import { Component, Input, OnChanges, ViewChild, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatSelectModule } from '@angular/material/select';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { AgGridAngular } from 'ag-grid-angular';
import type { ColDef, GridApi, GridReadyEvent } from 'ag-grid-community';
import { GridMatrix } from '../core/models';
import { divergentScale, signedScale } from './heatmap';

const SIGNED = new Set(['delta', 'gamma', 'vega', 'rho', 'theta']);

//! One (underlying, type) pivot rendered as a strike x maturity AG Grid, with a
//! metric selector (premium + any available Greeks) and a divergent heatmap + CSV.
@Component({
  selector: 'app-matrix-grid',
  standalone: true,
  imports: [
    FormsModule,
    MatFormFieldModule,
    MatSelectModule,
    MatButtonModule,
    MatIconModule,
    AgGridAngular,
  ],
  template: `
    <div class="mx-head">
      <span class="mx-title">{{ matrix.underlying }} · {{ matrix.type }}</span>
      <span class="thoth-spacer"></span>
      <mat-form-field appearance="outline" subscriptSizing="dynamic" class="metric">
        <mat-label>Metric</mat-label>
        <mat-select [(ngModel)]="metric" (selectionChange)="rebuild()">
          @for (m of metrics(); track m) {
            <mat-option [value]="m">{{ m }}</mat-option>
          }
        </mat-select>
      </mat-form-field>
      <button mat-stroked-button (click)="exportCsv()">
        <mat-icon>download</mat-icon> CSV
      </button>
    </div>
    <ag-grid-angular
      class="ag-theme-quartz grid"
      [rowData]="rowData()"
      [columnDefs]="colDefs()"
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
      .mx-head {
        display: flex;
        align-items: center;
        gap: 8px;
        margin-bottom: 6px;
      }
      .mx-title {
        font-weight: 600;
      }
      .metric {
        width: 160px;
      }
      .grid {
        width: 100%;
      }
    `,
  ],
})
export class MatrixGridComponent implements OnChanges {
  @Input({ required: true }) matrix!: GridMatrix;
  @ViewChild(AgGridAngular) gridCmp?: AgGridAngular;

  private api?: GridApi;

  readonly metric = signalDefault();
  readonly metrics = signal<string[]>(['premium']);
  readonly rowData = signal<Record<string, unknown>[]>([]);
  readonly colDefs = signal<ColDef[]>([]);

  ngOnChanges(): void {
    const greeks = Object.keys(this.matrix.greeks ?? {});
    this.metrics.set(['premium', ...greeks]);
    if (!this.metrics().includes(this.metric())) this.metric.set('premium');
    this.rebuild();
  }

  onReady(e: GridReadyEvent): void {
    this.api = e.api;
  }

  rebuild(): void {
    const m = this.matrix;
    const data: number[][] =
      this.metric() === 'premium' ? m.premium : (m.greeks?.[this.metric()] ?? []);
    const flat = data.flat().filter((v) => Number.isFinite(v));
    const scale = SIGNED.has(this.metric()) ? signedScale(flat) : divergentScale(flat);

    // rows = strikes, columns = maturities
    const rows = m.strikes.map((strike, r) => {
      const row: Record<string, unknown> = { strike };
      m.maturities.forEach((mat, c) => {
        row[mat] = data[r]?.[c];
      });
      return row;
    });

    const cols: ColDef[] = [
      {
        headerName: 'Strike',
        field: 'strike',
        pinned: 'left',
        width: 110,
        valueFormatter: (p) => fmt(p.value),
        cellStyle: { fontWeight: '600', background: 'var(--thoth-surface)' },
      },
      ...m.maturities.map<ColDef>((mat) => ({
        headerName: mat,
        field: mat,
        flex: 1,
        minWidth: 90,
        valueFormatter: (p) => fmt(p.value),
        cellStyle: (p) => ({
          backgroundColor: scale.bg(p.value as number),
          textAlign: 'right',
        }),
      })),
    ];

    this.rowData.set(rows);
    this.colDefs.set(cols);
  }

  exportCsv(): void {
    this.api?.exportDataAsCsv({
      fileName: `${this.matrix.underlying}_${this.matrix.type}_${this.metric()}.csv`,
    });
  }
}

function signalDefault() {
  return signal<string>('premium');
}

function fmt(v: unknown): string {
  if (v == null || !Number.isFinite(v as number)) return '';
  return (v as number).toLocaleString(undefined, {
    minimumFractionDigits: 4,
    maximumFractionDigits: 4,
  });
}
