import { Component, Input, computed, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { AgGridAngular } from 'ag-grid-angular';
import type { ColDef, ColGroupDef, ValueFormatterParams } from 'ag-grid-community';
import { OptionChain } from '../core/models';

const GREEKS = ['delta', 'gamma', 'vega', 'rho', 'theta'];

//! one grid row = one strike: the strike plus the call wing (c_*) and put wing (p_*) metrics.
interface ChainRow {
  strike: number;
  [metric: string]: number;
}

//! one rendered block = one maturity with its strike rows.
interface ChainBlock {
  maturity: string;
  rows: ChainRow[];
}

//! Option-chain view of one underlying: a block per maturity, with CALLS on the left and PUTS
//! on the right of a central Strike column, strikes running top-to-bottom (ascending). Each
//! wing shows premium + whatever per-cell Greeks the engine produced (every engine does), and
//! the premium sits next to the strike on both sides so the two wings mirror around it. Either
//! wing is omitted when the user priced only calls or only puts. The Greek columns fold away
//! behind a per-chain toggle (premium + strike stay) so the chain can collapse to a compact view.
@Component({
  selector: 'app-option-chain',
  standalone: true,
  imports: [MatButtonModule, MatIconModule, AgGridAngular],
  template: `
    <div class="oc-head">
      <span class="oc-name">{{ underlying() }}</span>
      <span class="oc-ccy thoth-muted">{{ currency() }}</span>
      <span class="thoth-spacer"></span>
      @if (hasGreeks()) {
        <button mat-stroked-button (click)="foldGreeks.set(!foldGreeks())">
          <mat-icon>{{ foldGreeks() ? 'unfold_more' : 'unfold_less' }}</mat-icon>
          {{ foldGreeks() ? 'Show Greeks' : 'Hide Greeks' }}
        </button>
      }
      <button mat-stroked-button (click)="exportCsv()"><mat-icon>download</mat-icon> CSV</button>
    </div>
    @for (block of blocks(); track block.maturity) {
      <div class="oc-block">
        <div class="oc-mat">{{ block.maturity }}</div>
        <ag-grid-angular
          class="ag-theme-quartz oc-grid"
          [rowData]="block.rows"
          [columnDefs]="cols()"
          [domLayout]="'autoHeight'"
        ></ag-grid-angular>
      </div>
    }
  `,
  styles: [
    `
      :host {
        display: block;
        margin-bottom: 28px;
      }
      .oc-head {
        display: flex;
        align-items: center;
        gap: 8px;
        margin-bottom: 8px;
      }
      .oc-name {
        font-weight: 600;
        font-size: 1.05rem;
      }
      .thoth-spacer {
        flex: 1;
      }
      .oc-block {
        margin-bottom: 16px;
      }
      .oc-mat {
        display: flex;
        align-items: center;
        justify-content: center;
        font-weight: 700;
        color: #fff;
        background: #1f2937;
        padding: 4px 10px;
        border-radius: 4px;
        margin-bottom: 4px;
      }
      .oc-grid {
        width: 100%;
      }
      /* ag-grid renders outside this component's view encapsulation, so reach its header/
         cell classes via ::ng-deep to tint the call/put wings and emphasise the strike. */
      /* centre every column + group header title — Calls/Puts/Strike/prem/delta/... (numeric
         cells stay right-aligned). The quartz theme pins the rightAligned headers to flex-end
         and loads after these component styles, so !important is needed to win; text-align
         centres the inner text element as a belt-and-suspenders fallback. */
      :host ::ng-deep .ag-header-cell-label,
      :host ::ng-deep .ag-header-group-cell-label {
        justify-content: center !important;
      }
      :host ::ng-deep .ag-header-cell-text,
      :host ::ng-deep .ag-header-group-text {
        text-align: center;
        width: 100%;
      }
      :host ::ng-deep .oc-calls .ag-header-group-cell-label {
        color: #1b8a4b;
        font-weight: 700;
      }
      :host ::ng-deep .oc-puts .ag-header-group-cell-label {
        color: #c2374a;
        font-weight: 700;
      }
      :host ::ng-deep .oc-strike-head {
        background: rgba(0, 0, 0, 0.05);
        font-weight: 700;
      }
      :host ::ng-deep .oc-strike {
        background: rgba(0, 0, 0, 0.04);
        font-weight: 700;
      }
      :host ::ng-deep .oc-prem {
        font-weight: 600;
      }
    `,
  ],
})
export class OptionChainComponent {
  //! @Input bridged into a signal so the columns/blocks/labels recompute reactively.
  private readonly chainSig = signal<OptionChain | null>(null);
  @Input({ required: true }) set chain(v: OptionChain) {
    this.chainSig.set(v);
  }

  //! Greeks visibility: when folded, only premium + strike columns are shown.
  readonly foldGreeks = signal(false);

  readonly underlying = computed(() => this.chainSig()?.underlying ?? '');
  readonly currency = computed(() => this.chainSig()?.currency ?? '');

  //! per-cell Greeks actually produced (identical on both wings — same engine).
  private readonly present = computed(() => {
    const c = this.chainSig();
    if (!c) return [];
    const side = c.call ?? c.put;
    return side ? GREEKS.filter((g) => side.greeks?.[g]) : [];
  });
  readonly hasGreeks = computed(() => this.present().length > 0);

  readonly cols = computed<(ColDef | ColGroupDef)[]>(() => {
    const c = this.chainSig();
    if (!c) return [];
    const present = this.foldGreeks() ? [] : this.present();
    const metrics = ['premium', ...present];

    // a wing = premium + Greeks under a CALLS/PUTS group. The call wing is reversed so its
    // premium ends up adjacent to the central Strike column, mirroring the put wing.
    const wing = (prefix: 'c' | 'p', label: string, reversed: boolean): ColGroupDef => {
      const order = reversed ? [...metrics].reverse() : metrics;
      return {
        headerName: label,
        headerClass: prefix === 'c' ? 'oc-calls' : 'oc-puts',
        children: order.map<ColDef>((metric) => ({
          headerName: metric === 'premium' ? 'prem' : metric,
          field: `${prefix}_${metric}`,
          width: metric === 'premium' ? 112 : 88,
          type: 'rightAligned',
          cellClass: metric === 'premium' ? 'oc-prem' : undefined,
          valueFormatter: fmt,
        })),
      };
    };

    const cols: (ColDef | ColGroupDef)[] = [];
    if (c.call) cols.push(wing('c', 'Calls', true));
    cols.push({
      headerName: 'Strike',
      field: 'strike',
      width: 100,
      type: 'rightAligned',
      headerClass: 'oc-strike-head',
      cellClass: 'oc-strike',
      valueFormatter: fmtStrike,
    });
    if (c.put) cols.push(wing('p', 'Puts', false));
    return cols;
  });

  readonly blocks = computed<ChainBlock[]>(() => {
    const c = this.chainSig();
    if (!c) return [];
    const present = this.present(); // rows always carry the Greeks; cols() decides visibility
    // strikes ascending (top -> bottom); keep the original column index i to read the matrices.
    const order = c.strikes.map((strike, i) => ({ strike, i })).sort((a, b) => a.strike - b.strike);
    return c.maturities.map((maturity, j) => {
      const rows = order.map(({ strike, i }) => {
        const row: ChainRow = { strike };
        if (c.call) {
          row['c_premium'] = c.call.premium[i]?.[j];
          for (const g of present) row[`c_${g}`] = c.call.greeks[g]?.[i]?.[j];
        }
        if (c.put) {
          row['p_premium'] = c.put.premium[i]?.[j];
          for (const g of present) row[`p_${g}`] = c.put.greeks[g]?.[i]?.[j];
        }
        return row;
      });
      return { maturity, rows };
    });
  });

  //! Export the whole chain (every maturity, all Greeks regardless of the fold state) as a flat
  //! CSV, built from the data directly so it needs no per-block ag-grid handle.
  exportCsv(): void {
    const c = this.chainSig();
    if (!c) return;
    const present = this.present();
    const sides: Array<['call' | 'put', GridSide]> = [];
    if (c.call) sides.push(['call', c.call]);
    if (c.put) sides.push(['put', c.put]);

    const head = ['maturity', 'strike'];
    for (const [name] of sides) {
      head.push(`${name}_premium`);
      for (const g of present) head.push(`${name}_${g}`);
    }
    const order = c.strikes.map((strike, i) => ({ strike, i })).sort((a, b) => a.strike - b.strike);
    const lines = [head.join(',')];
    c.maturities.forEach((maturity, j) => {
      for (const { strike, i } of order) {
        const cells = [maturity, String(strike)];
        for (const [, m] of sides) {
          cells.push(csvNum(m.premium[i]?.[j]));
          for (const g of present) cells.push(csvNum(m.greeks[g]?.[i]?.[j]));
        }
        lines.push(cells.join(','));
      }
    });

    const blob = new Blob([lines.join('\n')], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${c.underlying}_chain.csv`;
    a.click();
    URL.revokeObjectURL(url);
  }
}

//! the bits of a GridMatrix the CSV export reads.
interface GridSide {
  premium: number[][];
  greeks: Record<string, number[][]>;
}

function csvNum(v: number | undefined): string {
  return v == null || !Number.isFinite(v) ? '' : String(v);
}

function fmt(p: ValueFormatterParams): string {
  const v = p.value;
  if (v == null || !Number.isFinite(v as number)) return '';
  return (v as number).toLocaleString(undefined, { minimumFractionDigits: 4, maximumFractionDigits: 4 });
}

//! strike column: no forced decimals (90, 99.5), trimmed of trailing zeros.
function fmtStrike(p: ValueFormatterParams): string {
  const v = p.value;
  if (v == null || !Number.isFinite(v as number)) return '';
  return (v as number).toLocaleString(undefined, { maximumFractionDigits: 4 });
}
