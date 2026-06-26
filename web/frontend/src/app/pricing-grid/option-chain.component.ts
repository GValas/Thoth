import { Component, Input, computed, signal } from '@angular/core';
import { MatTabsModule } from '@angular/material/tabs';
import { AgGridAngular } from 'ag-grid-angular';
import type { CellClassParams, ColDef, ColGroupDef, ValueFormatterParams } from 'ag-grid-community';
import { GridMatrix, OptionChain } from '../core/models';

const GREEKS = ['delta', 'gamma', 'vega', 'rho', 'theta'];

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

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
  imports: [MatTabsModule, AgGridAngular],
  template: `
    <mat-tab-group class="oc-tabs" mat-stretch-tabs="false" animationDuration="0ms">
      @for (block of blocks(); track block.maturity) {
        <mat-tab [label]="fmtMaturity(block.maturity)">
          <div class="oc-block">
            <ag-grid-angular
              class="ag-theme-quartz oc-grid"
              [rowData]="block.rows"
              [columnDefs]="cols()"
              [context]="{ maturity: block.maturity }"
              [domLayout]="'autoHeight'"
            ></ag-grid-angular>
          </div>
        </mat-tab>
      }
    </mat-tab-group>
  `,
  styles: [
    `
      :host {
        display: block;
        margin-bottom: 28px;
      }
      .oc-tabs {
        margin-top: 4px;
      }
      .oc-block {
        padding-top: 12px;
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
        color: var(--thoth-positive);
        font-weight: 700;
      }
      :host ::ng-deep .oc-puts .ag-header-group-cell-label {
        color: var(--thoth-negative);
        font-weight: 700;
      }
      :host ::ng-deep .oc-strike-head {
        background: var(--thoth-surface-2);
        font-weight: 700;
      }
      :host ::ng-deep .oc-strike {
        background: var(--thoth-surface);
        font-weight: 700;
      }
      :host ::ng-deep .oc-prem {
        font-weight: 600;
      }
      /* live move tint on the value cells (premium + Greeks) */
      :host ::ng-deep .oc-up {
        color: var(--thoth-positive);
      }
      :host ::ng-deep .oc-down {
        color: var(--thoth-negative);
      }
    `,
  ],
})
export class OptionChainComponent {
  //! @Input bridged into a signal so the columns/blocks/labels recompute reactively.
  private readonly chainSig = signal<OptionChain | null>(null);
  @Input({ required: true }) set chain(v: OptionChain) {
    this.computeDirections(v); //!< before chainSig.set: compare to the previous values
    this.chainSig.set(v);
  }

  //! per-cell move direction (+1 up / -1 down) of the LAST update, keyed
  //! "<maturity>|<strike>|<field>"; drives the green/red cell tint while live re-pricing
  //! makes the premia/Greeks move (same idea as the live spot column on Market Data).
  private readonly dir = signal<Map<string, number>>(new Map());
  private readonly prevValue = new Map<string, number>();

  private dirKey(maturity: string, strike: number, field: string): string {
    return `${maturity}|${strike}|${field}`;
  }

  //! diff the incoming chain against the last one (per maturity/strike/field): cells that
  //! changed get +1/-1, unchanged ones drop out (no tint). A static grid never changes, so
  //! it stays untinted; live re-pricing repaints every tick.
  private computeDirections(next: OptionChain): void {
    const side = next.call ?? next.put;
    const metrics = ['premium', ...(side ? GREEKS.filter((g) => side.greeks?.[g]) : [])];
    const wings: Array<['c' | 'p', GridMatrix]> = [];
    if (next.call) wings.push(['c', next.call]);
    if (next.put) wings.push(['p', next.put]);

    const dirs = new Map<string, number>();
    next.maturities.forEach((maturity, j) => {
      next.strikes.forEach((strike, i) => {
        for (const [prefix, m] of wings) {
          for (const metric of metrics) {
            const v = metric === 'premium' ? m.premium[i]?.[j] : m.greeks[metric]?.[i]?.[j];
            if (v == null || !Number.isFinite(v)) continue;
            const key = this.dirKey(maturity, strike, `${prefix}_${metric}`);
            const prev = this.prevValue.get(key);
            if (prev !== undefined && v !== prev) dirs.set(key, Math.sign(v - prev));
            this.prevValue.set(key, v);
          }
        }
      });
    });
    this.dir.set(dirs);
  }

  //! direction for a rendered cell, from the grid context (maturity) + row (strike) + column.
  private cellDir(p: CellClassParams<ChainRow>): number {
    const maturity = (p.context as { maturity?: string } | undefined)?.maturity;
    const field = p.colDef.field;
    const strike = p.data?.strike;
    if (!maturity || !field || strike == null) return 0;
    return this.dir().get(this.dirKey(maturity, strike, field)) ?? 0;
  }

  //! Greeks visibility: when folded, only premium + strike columns are shown.
  //! per-cell Greeks actually produced (identical on both wings — same engine).
  private readonly present = computed(() => {
    const c = this.chainSig();
    if (!c) return [];
    const side = c.call ?? c.put;
    return side ? GREEKS.filter((g) => side.greeks?.[g]) : [];
  });

  readonly cols = computed<(ColDef | ColGroupDef)[]>(() => {
    const c = this.chainSig();
    if (!c) return [];
    const present = this.present();
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
          //! green/red tint on the last move (live re-pricing); static grids never change.
          cellClassRules: {
            'oc-up': (p: CellClassParams<ChainRow>) => this.cellDir(p) > 0,
            'oc-down': (p: CellClassParams<ChainRow>) => this.cellDir(p) < 0,
          },
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

  //! tab label: ISO maturity (YYYY-MM-DD) -> dd-MMM-yy, e.g. 2026-06-25 -> 25-Jun-26.
  //! parsed by splitting the string (no Date) so it never shifts a day across timezones.
  fmtMaturity(iso: string): string {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(iso);
    if (!m) return iso;
    const [, year, month, day] = m;
    const mon = MONTHS[Number(month) - 1] ?? month;
    return `${day}-${mon}-${year.slice(2)}`;
  }

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
