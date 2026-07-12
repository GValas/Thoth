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
import { InstrumentKind, OptionType } from '../core/models';
import { PricingPanelBase } from './pricing-panel.base';
import { PanelActionsComponent } from './panel-actions.component';
import { PanelResultComponent } from './panel-result.component';

type BarrierType = 'up&out' | 'up&in' | 'down&out' | 'down&in';
type Monitoring = 'continuous_monitoring' | 'discrete_monitoring';

//! Barrier option panel: price one knock-in/knock-out call or put with all its variations
//! (the four barrier types, continuous or discrete monitoring, barrier level, strike,
//! maturity, nominal), live off the feed. The single barrier level is routed to
//! barrier_up_level / barrier_down_level by the chosen barrier type.
@Component({
  selector: 'app-barrier-panel',
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
            <mat-button-toggle-group class="dense-toggle" [(ngModel)]="engine" aria-label="Engine">
              <mat-button-toggle value="ana">ana</mat-button-toggle>
              <mat-button-toggle value="pde">pde</mat-button-toggle>
              <mat-button-toggle value="mcl">mcl</mat-button-toggle>
              <mat-button-toggle value="mcl_gpu">mcl/gpu</mat-button-toggle>
            </mat-button-toggle-group>

            <mat-button-toggle-group class="dense-toggle" [(ngModel)]="type" aria-label="Type">
              <mat-button-toggle value="call">Call</mat-button-toggle>
              <mat-button-toggle value="put">Put</mat-button-toggle>
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

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="datepick">
              <mat-label>Maturity</mat-label>
              <input matInput [matDatepicker]="dp" [(ngModel)]="maturityDate" />
              <mat-datepicker-toggle matIconSuffix [for]="dp"></mat-datepicker-toggle>
              <mat-datepicker #dp></mat-datepicker>
            </mat-form-field>

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
            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="wide">
              <mat-label>Barrier type</mat-label>
              <mat-select [(ngModel)]="barrierType">
                <mat-option value="up&out">up &amp; out</mat-option>
                <mat-option value="up&in">up &amp; in</mat-option>
                <mat-option value="down&out">down &amp; out</mat-option>
                <mat-option value="down&in">down &amp; in</mat-option>
              </mat-select>
            </mat-form-field>

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Barrier level</mat-label>
              <input matInput type="number" [(ngModel)]="barrierLevel" />
            </mat-form-field>

            <mat-button-toggle-group class="dense-toggle" [(ngModel)]="monitoring" aria-label="Monitoring">
              <mat-button-toggle value="continuous_monitoring">Continuous</mat-button-toggle>
              <mat-button-toggle value="discrete_monitoring">Discrete</mat-button-toggle>
            </mat-button-toggle-group>

            @if (monitoring === 'discrete_monitoring') {
              <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
                <mat-label>Monitoring (days)</mat-label>
                <input matInput type="number" min="1" [(ngModel)]="monitoringPeriodDays" />
              </mat-form-field>
            }

            <mat-checkbox [(ngModel)]="absoluteStrike" matTooltip="Strike quoted in absolute price units rather than % of spot">
              Abs. strike
            </mat-checkbox>
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
export class BarrierPanelComponent extends PricingPanelBase implements OnInit {
  readonly kind: InstrumentKind = 'barrier';

  type: OptionType = 'call';
  strike = 100;
  nominal = 1;
  absoluteStrike = false;
  barrierType: BarrierType = 'up&out';
  barrierLevel = 120;
  monitoring: Monitoring = 'continuous_monitoring';
  monitoringPeriodDays = 1;

  ngOnInit(): void {
    this.init();
    this.applyPrefill();
  }

  protected override applyFields(i: Record<string, unknown>): void {
    if (typeof i['type'] === 'string') this.type = i['type'] as OptionType;
    if (typeof i['strike'] === 'number') this.strike = i['strike'];
    if (typeof i['nominal'] === 'number') this.nominal = i['nominal'];
    this.absoluteStrike = i['is_absolute_strike'] === true;
    if (typeof i['barrier_type'] === 'string') this.barrierType = i['barrier_type'] as BarrierType;
    if (typeof i['barrier_monitoring_type'] === 'string')
      this.monitoring = i['barrier_monitoring_type'] as Monitoring;
    const level = i['barrier_up_level'] ?? i['barrier_down_level'];
    if (typeof level === 'number') this.barrierLevel = level;
    if (typeof i['monitoring_period_days'] === 'number')
      this.monitoringPeriodDays = i['monitoring_period_days'];
  }

  protected buildFields(): Record<string, unknown> | null {
    const maturity = this.maturityIso;
    if (!this.underlying || maturity == null || !Number.isFinite(this.strike)) return null;
    if (!Number.isFinite(this.barrierLevel)) return null;
    const isDown = this.barrierType.startsWith('down');
    const fields: Record<string, unknown> = {
      underlying: this.underlying,
      strike: this.strike,
      maturity,
      type: this.type,
      nominal: this.nominal,
      barrier_type: this.barrierType,
      barrier_monitoring_type: this.monitoring,
      [isDown ? 'barrier_down_level' : 'barrier_up_level']: this.barrierLevel,
    };
    if (this.monitoring === 'discrete_monitoring') {
      fields['monitoring_period_days'] = this.monitoringPeriodDays;
    }
    if (this.absoluteStrike) fields['is_absolute_strike'] = true;
    return fields;
  }

  rowLabel(): string {
    return `${this.underlying} ${this.type} ${this.strike} ${this.barrierType}@${this.barrierLevel} ${this.maturityIso}`;
  }
}
