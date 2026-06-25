import { Component, Input } from '@angular/core';
import { MatCardModule } from '@angular/material/card';

const GREEK_KEYS = ['delta', 'gamma', 'vega', 'rho', 'theta'] as const;

//! Presentational result card shared by the three panels: a big premium figure plus the
//! per-contract Greeks the engine returned. Purely a view over inputs the panel feeds it.
@Component({
  selector: 'app-panel-result',
  standalone: true,
  imports: [MatCardModule],
  template: `
    @if (premium !== null) {
      <mat-card class="result-card" appearance="outlined">
        <mat-card-content>
          <div class="premium-row">
            <span class="label">Premium</span>
            <span class="value">{{ fmtPremium(premium) }}</span>
            <span class="ccy">{{ currency }}</span>
          </div>
          @if (greekList().length) {
            <div class="greeks">
              @for (g of greekList(); track g.key) {
                <div class="greek">
                  <span class="g-name">{{ g.key }}</span>
                  <span class="g-val">{{ fmtGreek(g.value) }}</span>
                </div>
              }
            </div>
          }
        </mat-card-content>
      </mat-card>
    }
  `,
  styles: [
    `
      .result-card {
        margin-bottom: 14px;
        max-width: 720px;
      }
      .premium-row {
        display: flex;
        align-items: baseline;
        gap: 10px;
      }
      .premium-row .label {
        font-size: 12px;
        color: var(--thoth-text-muted);
        text-transform: uppercase;
        letter-spacing: 0.04em;
      }
      .premium-row .value {
        font-size: 28px;
        font-weight: 700;
        color: var(--thoth-text);
        font-variant-numeric: tabular-nums;
      }
      .premium-row .ccy {
        font-size: 14px;
        color: var(--thoth-text-muted);
      }
      .greeks {
        display: flex;
        flex-wrap: wrap;
        gap: 18px;
        margin-top: 12px;
        padding-top: 10px;
        border-top: 1px solid var(--thoth-border);
      }
      .greek {
        display: flex;
        flex-direction: column;
        min-width: 70px;
      }
      .g-name {
        font-size: 11px;
        text-transform: uppercase;
        letter-spacing: 0.04em;
        color: var(--thoth-text-muted);
      }
      .g-val {
        font-size: 15px;
        font-weight: 600;
        font-variant-numeric: tabular-nums;
      }
    `,
  ],
})
export class PanelResultComponent {
  @Input() premium: number | null = null;
  @Input() greeks: Partial<Record<string, number>> = {};
  @Input() currency = '';

  greekList(): Array<{ key: string; value: number }> {
    return GREEK_KEYS.filter((k) => this.greeks[k] != null).map((k) => ({
      key: k,
      value: this.greeks[k] as number,
    }));
  }

  fmtPremium(v: number): string {
    if (v == null || !Number.isFinite(v)) return '—';
    return v.toLocaleString(undefined, { minimumFractionDigits: 4, maximumFractionDigits: 4 });
  }

  fmtGreek(v: number): string {
    if (v == null || !Number.isFinite(v)) return '—';
    return v.toLocaleString(undefined, { maximumFractionDigits: 6 });
  }
}
