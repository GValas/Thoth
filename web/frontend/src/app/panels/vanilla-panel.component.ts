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
import { InstrumentKind, OptionType, Exercise } from '../core/models';
import { PricingPanelBase } from './pricing-panel.base';
import { PanelActionsComponent } from './panel-actions.component';
import { PanelResultComponent } from './panel-result.component';

//! Vanilla option panel: price one European/American call or put with all its variations
//! (strike, maturity, nominal, absolute vs relative strike) and Greeks, live off the feed.
@Component({
  selector: 'app-vanilla-panel',
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

            <mat-button-toggle-group class="dense-toggle" [(ngModel)]="exercise" aria-label="Exercise">
              <mat-button-toggle value="european">European</mat-button-toggle>
              <mat-button-toggle value="american">American</mat-button-toggle>
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
export class VanillaPanelComponent extends PricingPanelBase implements OnInit {
  readonly kind: InstrumentKind = 'vanilla';

  type: OptionType = 'call';
  exercise: Exercise = 'european';
  strike = 100;
  nominal = 1;
  absoluteStrike = false;

  ngOnInit(): void {
    this.init();
    this.applyPrefill();
  }

  protected override applyFields(i: Record<string, unknown>): void {
    if (typeof i['type'] === 'string') this.type = i['type'] as OptionType;
    if (typeof i['exercise'] === 'string') this.exercise = i['exercise'] as Exercise;
    if (typeof i['strike'] === 'number') this.strike = i['strike'];
    if (typeof i['nominal'] === 'number') this.nominal = i['nominal'];
    this.absoluteStrike = i['is_absolute_strike'] === true;
  }

  protected buildFields(): Record<string, unknown> | null {
    const maturity = this.maturityIso;
    if (!this.underlying || maturity == null || !Number.isFinite(this.strike)) return null;
    const fields: Record<string, unknown> = {
      underlying: this.underlying,
      strike: this.strike,
      maturity,
      type: this.type,
      exercise: this.exercise,
      nominal: this.nominal,
    };
    if (this.absoluteStrike) fields['is_absolute_strike'] = true;
    return fields;
  }

  rowLabel(): string {
    return `${this.underlying} ${this.type} ${this.strike} ${this.maturityIso} (${this.exercise})`;
  }
}
