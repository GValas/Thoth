import { Component, Input, computed } from '@angular/core';
import { AgGridAngular } from 'ag-grid-angular';
import type { CellClassParams, ColDef, ValueFormatterParams } from 'ag-grid-community';
import { buildCorrelModel, MarketModel, round, writeCorrel } from './market-model';
import { signedScale } from '../pricing-grid/heatmap';

//! Correlation area: an editable N×N matrix over [equities (+ `_var` for Heston), fx].
//! The matrix is symmetric (editing (i,j) mirrors (j,i)) with a locked unit diagonal; it
//! auto-resizes as equities/fx are added. Edits persist into the single `correl` object.
@Component({
  selector: 'app-correlation-section',
  standalone: true,
  imports: [AgGridAngular],
  template: `
    @if (cm().members.length) {
      <ag-grid-angular
        class="ag-theme-quartz grid"
        [rowData]="rows()"
        [columnDefs]="cols()"
        [domLayout]="'autoHeight'"
        [getRowId]="rowId"
        (cellValueChanged)="onCell($event)"
      ></ag-grid-angular>
      <p class="hint">Symmetric · diagonal locked at 1 · values in [-1, 1]</p>
    } @else {
      <p class="empty">Add equities or fx pairs to populate the correlation matrix.</p>
    }
  `,
  styles: [
    `
      :host {
        display: block;
      }
      .grid {
        width: 100%;
      }
      .hint {
        color: var(--thoth-text-muted, #888);
        font-size: 12px;
      }
      .empty {
        color: var(--thoth-text-muted, #888);
      }
    `,
  ],
})
export class CorrelationSectionComponent {
  @Input({ required: true }) model!: MarketModel;

  private readonly scale = signedScale([1, -1]); // fixed [-1,1] domain

  readonly cm = computed(() => buildCorrelModel(this.model));

  readonly rows = computed<Record<string, unknown>[]>(() => {
    const { members, matrix } = this.cm();
    return members.map((name, i) => {
      const row: Record<string, unknown> = { __name: name, __i: i };
      members.forEach((_, j) => (row[`c${j}`] = matrix[i][j]));
      return row;
    });
  });

  readonly cols = computed<ColDef[]>(() => {
    const { members } = this.cm();
    return [
      {
        headerName: '',
        field: '__name',
        pinned: 'left',
        width: 130,
        cellStyle: { fontWeight: '600', background: 'var(--thoth-surface)' },
      },
      ...members.map<ColDef>((m, j) => ({
        headerName: m,
        field: `c${j}`,
        width: 84,
        editable: (p) => (p.data as { __i: number }).__i !== j,
        cellEditor: 'agNumberCellEditor',
        valueParser: (p) => Math.max(-1, Math.min(1, Number(p.newValue))),
        valueFormatter: (p: ValueFormatterParams) =>
          Number.isFinite(p.value) ? (p.value as number).toFixed(2) : '',
        cellStyle: (p: CellClassParams) => ({
          textAlign: 'right',
          backgroundColor: this.scale.bg(p.value as number),
        }),
      })),
    ];
  });

  rowId = (p: { data: { __name: string } }) => p.data.__name;

  onCell(e: { data: Record<string, unknown>; colDef: ColDef; newValue: unknown }): void {
    const i = e.data['__i'] as number;
    const j = Number((e.colDef.field ?? 'c0').slice(1));
    const v = round(Math.max(-1, Math.min(1, Number(e.newValue))), 4);
    const { members, matrix } = this.cm();
    const next = matrix.map((row) => [...row]);
    next[i][j] = v;
    next[j][i] = v; // mirror
    writeCorrel(this.model, { members, matrix: next });
  }
}
