import { Component, Input } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { CurveGridComponent } from './curve-grid.component';
import { defaultCurve, MarketModel } from './market-model';

const CCY_POOL = ['eur', 'usd', 'jpy', 'gbp', 'chf', 'cad'];

//! Rates area: one editable yield curve (dates + rates in %) per currency.
@Component({
  selector: 'app-rates-section',
  standalone: true,
  imports: [MatButtonModule, MatIconModule, MatTooltipModule, CurveGridComponent],
  template: `
    <div class="head">
      <button mat-stroked-button (click)="addCurrency()"><mat-icon>add</mat-icon> Currency</button>
      <span class="hint">Yield-curve rates are in percent</span>
    </div>
    @for (c of model.currencies(); track c.name) {
      <div class="ccy">
        <div class="ccy-head">
          <strong>{{ c.name }}</strong>
          <span class="rate-ref">rate: {{ c.payload['rate'] }}</span>
          <button mat-icon-button color="warn" (click)="remove(c.name)" matTooltip="Remove currency">
            <mat-icon>delete</mat-icon>
          </button>
        </div>
        @if (rateName(c.payload['rate']); as rn) {
          <app-curve-grid [model]="model" [name]="rn" valueLabel="Rate (%)"></app-curve-grid>
        }
      </div>
    }
    @if (model.currencies().length === 0) {
      <p class="empty">No currencies yet — add one or generate sample data.</p>
    }
  `,
  styles: [
    `
      :host {
        display: block;
      }
      .head {
        display: flex;
        align-items: center;
        gap: 8px;
        margin-bottom: 12px;
      }
      .hint {
        color: var(--thoth-text-muted, #888);
        font-size: 12px;
      }
      .ccy {
        margin-bottom: 18px;
      }
      .ccy-head {
        display: flex;
        align-items: center;
        gap: 12px;
        margin-bottom: 6px;
      }
      .rate-ref {
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
  @Input() today = '2026-01-01';

  rateName(ref: unknown): string | null {
    return typeof ref === 'string' && this.model.has(ref) ? ref : null;
  }

  addCurrency(): void {
    const code = CCY_POOL.find((c) => !this.model.has(c)) ?? this.model.freshName('ccy');
    this.model.upsert({
      name: `${code}_rate`,
      kind: 'yield_curve',
      payload: defaultCurve(this.today, 2),
    });
    this.model.upsert({ name: code, kind: 'currency', payload: { rate: `${code}_rate` } });
    this.model.syncForex(); // a new currency yields a new fx pair vs the base
  }

  remove(name: string): void {
    const rate = this.model.byName(name)?.payload['rate'];
    this.model.remove(name, ...(typeof rate === 'string' ? [rate] : []));
    this.model.syncForex(); // drop fx pairs that referenced the removed currency
  }
}
