import { Component, OnInit } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatCardModule } from '@angular/material/card';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatSelectModule } from '@angular/material/select';
import { MatInputModule } from '@angular/material/input';
import { MatButtonModule } from '@angular/material/button';
import { MatButtonToggleModule } from '@angular/material/button-toggle';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { MatDatepickerModule } from '@angular/material/datepicker';
import { provideNativeDateAdapter } from '@angular/material/core';
import { Engine, InstrumentKind } from '../core/models';
import { PricingPanelBase } from './pricing-panel.base';
import { PanelActionsComponent } from './panel-actions.component';
import { PanelResultComponent } from './panel-result.component';

//! Ratchet (cliquet) note panel: a coupon built from the period returns of the
//! underlying, each clipped to [local_floor, local_cap] and locked in, the sum
//! floored (capital protection) and optionally capped. Path-dependent ->
//! Monte-Carlo only. Floors/caps are percent; global floor defaults to 0.
@Component({
  selector: 'app-ratchet-panel',
  standalone: true,
  imports: [
    FormsModule,
    MatCardModule,
    MatFormFieldModule,
    MatSelectModule,
    MatInputModule,
    MatButtonModule,
    MatButtonToggleModule,
    MatCheckboxModule,
    MatIconModule,
    MatTooltipModule,
    MatDatepickerModule,
    PanelActionsComponent,
    PanelResultComponent,
  ],
  providers: [provideNativeDateAdapter()],
  template: `
    @if (ctx.workspace()) {
      <mat-card class="panel-card" appearance="outlined">
        <mat-card-content>
          <div class="row">
            <!-- path-dependent: Monte-Carlo only -->
            <mat-button-toggle-group class="dense-toggle" [(ngModel)]="engine" aria-label="Engine">
              <mat-button-toggle value="mcl">mcl</mat-button-toggle>
              <mat-button-toggle value="mcl_gpu">mcl/gpu</mat-button-toggle>
            </mat-button-toggle-group>
          </div>

          <div class="row">
            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="wide">
              <mat-label>Underlying</mat-label>
              <mat-select [(ngModel)]="underlying">
                @for (o of ctx.underlyingObjects(); track o.name) {
                  <mat-option [value]="o.name">{{ o.name }} ({{ o.kind }})</mat-option>
                }
              </mat-select>
            </mat-form-field>
            <button mat-icon-button type="button" (click)="refresh()" matTooltip="Reload underlyings from saved market data">
              <mat-icon>refresh</mat-icon>
            </button>

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Nominal</mat-label>
              <input matInput type="number" [(ngModel)]="nominal" />
            </mat-form-field>

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Period (days)</mat-label>
              <input matInput type="number" min="1" [(ngModel)]="observationPeriodDays" />
            </mat-form-field>

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="ccy">
              <mat-label>Ccy</mat-label>
              <mat-select [(ngModel)]="currency">
                @for (c of ctx.currencyNames(); track c) {
                  <mat-option [value]="c">{{ c }}</mat-option>
                }
              </mat-select>
            </mat-form-field>
          </div>

          <div class="row">
            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Local floor (%)</mat-label>
              <input matInput type="number" [(ngModel)]="localFloor" matTooltip="Per-period return floor" />
            </mat-form-field>
            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Local cap (%)</mat-label>
              <input matInput type="number" [(ngModel)]="localCap" matTooltip="Per-period return cap (locked in)" />
            </mat-form-field>
            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Global floor (%)</mat-label>
              <input matInput type="number" [(ngModel)]="globalFloor" matTooltip="Coupon floor — the capital protection" />
            </mat-form-field>

            <mat-checkbox [(ngModel)]="useGlobalCap" matTooltip="Cap the summed coupon">
              Global cap
            </mat-checkbox>
            @if (useGlobalCap) {
              <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
                <mat-label>Global cap (%)</mat-label>
                <input matInput type="number" [(ngModel)]="globalCap" />
              </mat-form-field>
            }
          </div>

          <div class="row">
            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="datepick">
              <mat-label>Maturity</mat-label>
              <input matInput [matDatepicker]="dp" [(ngModel)]="maturityDate" />
              <mat-datepicker-toggle matIconSuffix [for]="dp"></mat-datepicker-toggle>
              <mat-datepicker #dp></mat-datepicker>
            </mat-form-field>
          </div>

          <app-panel-actions [panel]="this"></app-panel-actions>
        </mat-card-content>
      </mat-card>

      <app-panel-result [premium]="premium()" [greeks]="greeks()" [currency]="resultCurrency()"></app-panel-result>
    } @else {
      <p class="thoth-muted">Loading…</p>
    }
  `,
  styleUrl: './panel.shared.scss',
})
export class RatchetPanelComponent extends PricingPanelBase implements OnInit {
  readonly kind: InstrumentKind = 'ratchet';
  //! ratchet notes carry no per-contract Greeks worth showing (a coupon on
  //! clipped returns) — request premium only
  override readonly supportsGreeks = false;

  //! path-dependent: Monte-Carlo only
  override engine: Engine = 'mcl';
  nominal = 100;
  observationPeriodDays = 90;
  localFloor = -5;
  localCap = 5;
  globalFloor = 0;
  useGlobalCap = false;
  globalCap = 30;

  ngOnInit(): void {
    this.init();
  }

  protected buildFields(): Record<string, unknown> | null {
    const maturity = this.maturityIso;
    if (!this.underlying || maturity == null) return null;
    if (this.observationPeriodDays < 1) return null;
    if (this.localCap < this.localFloor) return null; //!< engine rejects it too
    const fields: Record<string, unknown> = {
      underlying: this.underlying,
      maturity,
      nominal: this.nominal,
      observation_period_days: this.observationPeriodDays,
      local_floor: this.localFloor,
      local_cap: this.localCap,
      global_floor: this.globalFloor,
    };
    if (this.useGlobalCap) fields['global_cap'] = this.globalCap;
    return fields;
  }

  rowLabel(): string {
    return `${this.underlying} ratchet [${this.localFloor},${this.localCap}]% ${this.maturityIso}`;
  }
}
