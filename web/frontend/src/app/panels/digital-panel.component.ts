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

//! Digital (binary) option panel: cash-or-nothing (pays a fixed cash amount) or
//! asset-or-nothing (pays the spot) iff in the money at maturity. Path-independent, so it
//! prices on all three engines — ANA (closed form, the default and exact), PDE and MCL.
//! Strike absolute or relative (percent of spot). NB: a digital struck at the money is the
//! classic discontinuity worst case for the PDE grid; ANA is exact.
@Component({
  selector: 'app-digital-panel',
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
              <mat-button-toggle value="call">call</mat-button-toggle>
              <mat-button-toggle value="put">put</mat-button-toggle>
            </mat-button-toggle-group>

            <mat-button-toggle-group class="dense-toggle" [(ngModel)]="payout" aria-label="Payout">
              <mat-button-toggle value="cash_or_nothing" matTooltip="Pays a fixed cash amount if in the money">cash</mat-button-toggle>
              <mat-button-toggle value="asset_or_nothing" matTooltip="Pays the spot if in the money">asset</mat-button-toggle>
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
            @if (payout === 'cash_or_nothing') {
              <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
                <mat-label>Cash amount (Q)</mat-label>
                <input matInput type="number" [(ngModel)]="cashAmount" matTooltip="Fixed payout if in the money" />
              </mat-form-field>
            }

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
export class DigitalPanelComponent extends PricingPanelBase implements OnInit {
  readonly kind: InstrumentKind = 'digital';

  //! path-independent -> ANA closed form is the default (and exact)
  override engine: Engine = 'ana';
  type: OptionType = 'call';
  strike = 100;
  absoluteStrike = true;
  payout: 'cash_or_nothing' | 'asset_or_nothing' = 'cash_or_nothing';
  cashAmount = 1;

  ngOnInit(): void {
    this.init();
    this.applyPrefill();
  }

  protected override applyFields(i: Record<string, unknown>): void {
    if (typeof i['type'] === 'string') this.type = i['type'] as OptionType;
    if (typeof i['strike'] === 'number') this.strike = i['strike'];
    this.absoluteStrike = i['is_absolute_strike'] === true;
    if (i['payout'] === 'asset_or_nothing' || i['payout'] === 'cash_or_nothing') this.payout = i['payout'];
    if (typeof i['cash_amount'] === 'number') this.cashAmount = i['cash_amount'];
  }

  protected buildFields(): Record<string, unknown> | null {
    const maturity = this.maturityIso;
    if (!this.underlying || maturity == null || !Number.isFinite(this.strike)) return null;
    const fields: Record<string, unknown> = {
      underlying: this.underlying,
      strike: this.strike,
      is_absolute_strike: this.absoluteStrike,
      maturity,
      type: this.type,
      payout: this.payout,
    };
    if (this.payout === 'cash_or_nothing') fields['cash_amount'] = this.cashAmount;
    return fields;
  }

  rowLabel(): string {
    const p = this.payout === 'cash_or_nothing' ? 'digital' : 'asset-digital';
    return `${this.underlying} ${p} ${this.type} ${this.strike}${this.absoluteStrike ? '' : '%'} ${this.maturityIso}`;
  }
}
