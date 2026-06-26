import { Component, Input, computed } from '@angular/core';
import { AgGridAngular } from 'ag-grid-angular';
import type { CellValueChangedEvent, ColDef, GridApi, GridReadyEvent } from 'ag-grid-community';
import { MarketModel } from './market-model';

//! one grid row = one shared pillar: its date plus each currency's rate (%) at that pillar.
interface PillarRow {
  date: string;
  [ccy: string]: string | number;
}

//! Rates area: ALL currencies share one pillar set, edited together in a single grouped
//! table (rows = pillars, columns = the currencies). Every edit rewrites every currency's
//! yield_curve with the same `dates` array, so the currencies can never drift out of pillar
//! alignment. Currencies are fixed (no add/remove); only the rates and the shared pillar
//! grid are editable.
@Component({
  selector: 'app-rates-section',
  standalone: true,
  imports: [AgGridAngular],
  template: `
    @if (model.currencies().length) {
      <ag-grid-angular
        class="ag-theme-quartz grid"
        [rowData]="rows()"
        [columnDefs]="cols()"
        [domLayout]="'autoHeight'"
        (gridReady)="onReady($event)"
        (cellValueChanged)="onCell($event)"
      ></ag-grid-angular>
      <div class="pillar-actions">
        <span class="hint">All currencies share these pillars · rates in percent</span>
      </div>
    } @else {
      <p class="empty">No currencies — generate the market data.</p>
    }
  `,
  styles: [
    `
      :host {
        display: block;
      }
      .grid {
        width: 100%;
        max-width: 640px;
      }
      .pillar-actions {
        display: flex;
        align-items: center;
        gap: 8px;
        margin-top: 6px;
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
export class RatesSectionComponent {
  @Input({ required: true }) model!: MarketModel;

  private api?: GridApi;

  //! the rate-curve object name backing a currency (the currency's `rate` reference).
  private rateName(ccyName: string): string | null {
    const ref = this.model.byName(ccyName)?.payload['rate'];
    return typeof ref === 'string' && this.model.has(ref) ? ref : null;
  }

  //! canonical shared pillar dates: the longest currency curve wins (data is normally already
  //! aligned; this only matters for legacy/ragged input, which the next edit re-aligns).
  private canonicalDates(): string[] {
    let dates: string[] = [];
    for (const c of this.model.currencies()) {
      const rn = this.rateName(c.name);
      const d = (rn && (this.model.byName(rn)!.payload['dates'] as string[])) || [];
      if (d.length > dates.length) dates = d;
    }
    return dates;
  }

  readonly rows = computed<PillarRow[]>(() => {
    const dates = this.canonicalDates();
    const ccys = this.model.currencies();
    return dates.map((date, i) => {
      const row: PillarRow = { date };
      for (const c of ccys) {
        const rn = this.rateName(c.name);
        const vals = (rn && (this.model.byName(rn)!.payload['values'] as number[])) || [];
        row[c.name] = vals[i] ?? 0;
      }
      return row;
    });
  });

  readonly cols = computed<ColDef[]>(() => [
    { headerName: 'Pillar', field: 'date', editable: true, width: 150 },
    ...this.model.currencies().map<ColDef>((c) => ({
      headerName: `${c.name.toUpperCase()} (%)`,
      field: c.name,
      editable: true,
      cellEditor: 'agNumberCellEditor',
      valueParser: (p) => Number(p.newValue),
      type: 'rightAligned',
      flex: 1,
      minWidth: 100,
    })),
  ]);

  onReady(e: GridReadyEvent): void {
    this.api = e.api;
  }

  //! any edit (a date or a rate) rewrites every currency's curve from the full grid, so all
  //! currencies keep the same `dates` array and an equal pillar count.
  onCell(_e: CellValueChangedEvent<PillarRow>): void {
    this.commitGrid();
  }

  private commitGrid(): void {
    const rows: PillarRow[] = [];
    this.api?.forEachNode((n) => rows.push(n.data as PillarRow));
    const live = rows.length ? rows : this.rows();
    this.writeAll(
      live.map((r) => r.date),
      (ccy) => live.map((r) => Number(r[ccy])),
    );
  }

  //! write the shared `dates` and a per-currency value column into every currency's curve.
  private writeAll(dates: string[], valuesOf: (ccy: string) => number[]): void {
    for (const c of this.model.currencies()) {
      const rn = this.rateName(c.name);
      if (rn) this.model.patch(rn, { dates, values: valuesOf(c.name) });
    }
  }

}
