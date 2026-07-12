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
import { Engine, InstrumentKind, OptionType } from '../core/models';
import { PricingPanelBase } from './pricing-panel.base';
import { PanelActionsComponent } from './panel-actions.component';
import { PanelResultComponent } from './panel-result.component';

//! Asian (arithmetic average-price) option panel: the payoff is on the average of
//! the underlying over the observation schedule rather than the final level, so it
//! prices below the equivalent vanilla. Path-dependent -> Monte-Carlo only (ANA/PDE
//! reject). Strike absolute or relative (percent of spot).
@Component({
  selector: 'app-asian-panel',
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

            <mat-button-toggle-group class="dense-toggle" [(ngModel)]="type" aria-label="Type">
              <mat-button-toggle value="call">call</mat-button-toggle>
              <mat-button-toggle value="put">put</mat-button-toggle>
            </mat-button-toggle-group>

            <mat-checkbox [(ngModel)]="includeGreeks">Greeks</mat-checkbox>
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
              <mat-label>Strike</mat-label>
              <input matInput type="number" [(ngModel)]="strike" />
            </mat-form-field>
            <mat-checkbox [(ngModel)]="absoluteStrike" matTooltip="Off: strike is a percent of spot">
              Absolute strike
            </mat-checkbox>

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Nominal</mat-label>
              <input matInput type="number" [(ngModel)]="nominal" />
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
              <mat-label>Averaging period (days)</mat-label>
              <input matInput type="number" min="1" [(ngModel)]="observationPeriodDays" />
            </mat-form-field>

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
export class AsianPanelComponent extends PricingPanelBase implements OnInit {
  readonly kind: InstrumentKind = 'asian';

  //! path-dependent: Monte-Carlo only
  override engine: Engine = 'mcl';
  type: OptionType = 'call';
  strike = 100;
  absoluteStrike = true;
  nominal = 1;
  observationPeriodDays = 30;

  ngOnInit(): void {
    this.init();
    this.applyPrefill();
  }

  protected override applyFields(i: Record<string, unknown>): void {
    if (typeof i['type'] === 'string') this.type = i['type'] as OptionType;
    if (typeof i['strike'] === 'number') this.strike = i['strike'];
    if (typeof i['nominal'] === 'number') this.nominal = i['nominal'];
    this.absoluteStrike = i['is_absolute_strike'] === true;
    if (typeof i['observation_period_days'] === 'number')
      this.observationPeriodDays = i['observation_period_days'];
  }

  protected buildFields(): Record<string, unknown> | null {
    const maturity = this.maturityIso;
    if (!this.underlying || maturity == null || !Number.isFinite(this.strike)) return null;
    if (this.observationPeriodDays < 1) return null;
    const fields: Record<string, unknown> = {
      underlying: this.underlying,
      strike: this.strike,
      maturity,
      type: this.type,
      nominal: this.nominal,
      observation_period_days: this.observationPeriodDays,
    };
    if (this.absoluteStrike) fields['is_absolute_strike'] = true;
    else fields['is_absolute_strike'] = false;
    return fields;
  }

  rowLabel(): string {
    return `${this.underlying} Asian ${this.type} ${this.strike}${this.absoluteStrike ? '' : '%'} ${this.maturityIso}`;
  }
}
