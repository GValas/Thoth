import { Component, Input, computed, inject } from '@angular/core';
import { AgGridAngular } from 'ag-grid-angular';
import type { CellClassParams, ColDef, ValueFormatterParams } from 'ag-grid-community';
import { buildCorrelModel, CorrelModel, MarketModel, round, writeCorrel } from './market-model';
import { signedScale } from '../pricing-grid/heatmap';
import { LiveSpotsService } from './live-spots.service';
import { LiveCorrelService } from './live-correl.service';

//! Correlation area: an editable N×N matrix over [equities (+ `_var` for Heston), fx].
//! The matrix is symmetric (editing (i,j) mirrors (j,i)) with a locked unit diagonal; it
//! auto-resizes as equities/fx are added. Edits persist into the single `correl` object.
//! When Live is on, the matrix shows the streaming correlation (read-only) sliced out of the
//! universe matrix by member name, falling back to the stored value for any unstreamed member.
@Component({
  selector: 'app-correlation-section',
  standalone: true,
  imports: [AgGridAngular],
  template: `
    @if (cm().members.length) {
      <ag-grid-angular
        class="ag-theme-quartz grid"
        [style.width.px]="gridWidth()"
        [rowData]="rows()"
        [columnDefs]="cols()"
        [domLayout]="'autoHeight'"
        [getRowId]="rowId"
        (cellValueChanged)="onCell($event)"
      ></ag-grid-angular>
      @if (liveOn()) {
        <p class="hint">Live correlation — read-only · symmetric · valid (PSD) every tick</p>
      } @else {
        <p class="hint">Symmetric · diagonal locked at 1 · values in [-1, 1]</p>
      }
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
        max-width: 100%;
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
  private readonly live = inject(LiveSpotsService);
  private readonly liveCorrel = inject(LiveCorrelService);

  readonly cm = computed(() => buildCorrelModel(this.model));

  //! true while the live feed is driving the matrix (then the grid is read-only).
  readonly liveOn = computed(() => this.live.enabled() && this.liveCorrel.snap() !== null);

  //! the matrix to display: stored values, overlaid with the live correlation for any pair
  //! whose members are both streamed (reads the live signal so the grid re-renders on a tick).
  readonly displayCm = computed<CorrelModel>(() => {
    const { members, matrix } = this.cm();
    if (!this.liveOn()) return { members, matrix };
    const overlaid = members.map((a, i) =>
      members.map((b, j) => {
        if (i === j) return 1;
        const v = this.liveCorrel.value(a, b);
        return v === undefined ? matrix[i][j] : round(v);
      }),
    );
    return { members, matrix: overlaid };
  });

  readonly rows = computed<Record<string, unknown>[]>(() => {
    const { members, matrix } = this.displayCm();
    return members.map((name, i) => {
      const row: Record<string, unknown> = { __name: name, __i: i };
      members.forEach((_, j) => (row[`c${j}`] = matrix[i][j]));
      return row;
    });
  });

  readonly cols = computed<ColDef[]>(() => {
    const { members } = this.displayCm();
    const liveOn = this.liveOn();
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
        //! locked diagonal, and the whole grid is read-only while the live feed drives it.
        editable: (p) => !liveOn && (p.data as { __i: number }).__i !== j,
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

  //! shrink the grid to its content: pinned label column (130) + 84px per member (+2 borders),
  //! so the matrix no longer stretches across the empty space on the right.
  readonly gridWidth = computed(() => 130 + this.displayCm().members.length * 84 + 2);

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
