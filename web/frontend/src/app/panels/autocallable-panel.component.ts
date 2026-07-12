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

//! Autocallable (structured note) panel: an Athena snowball or a Phoenix
//! conditional-coupon note, with the memory variant. The autocall observation
//! schedule is generated from a first date + a monthly frequency + a count
//! (every date strictly between today and maturity), matching how these notes
//! are typically struck. Levels are percent of spot (like the engine's
//! sticky-cash convention). No ANA closed form — engines are pde / mcl only.
@Component({
  selector: 'app-autocallable-panel',
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
            <!-- ANA has no closed form for an autocallable: pde / mcl only -->
            <mat-button-toggle-group class="dense-toggle" [(ngModel)]="engine" aria-label="Engine">
              <mat-button-toggle value="pde">pde</mat-button-toggle>
              <mat-button-toggle value="mcl">mcl</mat-button-toggle>
            </mat-button-toggle-group>

            <mat-button-toggle-group class="dense-toggle" [(ngModel)]="flavour" aria-label="Flavour">
              <mat-button-toggle value="athena" matTooltip="Snowball coupon paid on early redemption">
                Athena
              </mat-button-toggle>
              <mat-button-toggle value="phoenix" matTooltip="Conditional coupon paid per observation">
                Phoenix
              </mat-button-toggle>
            </mat-button-toggle-group>

            @if (flavour === 'phoenix') {
              <mat-checkbox [(ngModel)]="couponMemory" matTooltip="Recover consecutively missed coupons">
                Memory
              </mat-checkbox>
            }
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
              <mat-label>Coupon (%)</mat-label>
              <input matInput type="number" [(ngModel)]="coupon" [matTooltip]="flavour === 'athena' ? 'Snowball rate per observation' : 'Conditional coupon per observation'" />
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
              <mat-label>Autocall barrier (% spot)</mat-label>
              <input matInput type="number" [(ngModel)]="autocallBarrier" />
            </mat-form-field>

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Protection barrier (% spot)</mat-label>
              <input matInput type="number" [(ngModel)]="protectionBarrier" />
            </mat-form-field>

            @if (flavour === 'phoenix') {
              <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
                <mat-label>Coupon barrier (% spot)</mat-label>
                <input matInput type="number" [(ngModel)]="couponBarrier" />
              </mat-form-field>
            }
          </div>

          <div class="row">
            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="datepick">
              <mat-label>First observation</mat-label>
              <input matInput [matDatepicker]="fo" [(ngModel)]="firstObservation" />
              <mat-datepicker-toggle matIconSuffix [for]="fo"></mat-datepicker-toggle>
              <mat-datepicker #fo></mat-datepicker>
            </mat-form-field>

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Frequency (months)</mat-label>
              <input matInput type="number" min="1" [(ngModel)]="frequencyMonths" />
            </mat-form-field>

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="num">
              <mat-label>Observations</mat-label>
              <input matInput type="number" min="1" [(ngModel)]="observations" />
            </mat-form-field>

            <mat-form-field appearance="outline" subscriptSizing="dynamic" class="datepick">
              <mat-label>Maturity</mat-label>
              <input matInput [matDatepicker]="dp" [(ngModel)]="maturityDate" />
              <mat-datepicker-toggle matIconSuffix [for]="dp"></mat-datepicker-toggle>
              <mat-datepicker #dp></mat-datepicker>
            </mat-form-field>
          </div>

          @if (scheduleDates().length) {
            <p class="meta thoth-muted">
              {{ scheduleDates().length }} autocall date(s): {{ scheduleDates().join(', ') }}
            </p>
          } @else {
            <p class="thoth-error">
              No autocall date falls strictly between today and maturity — adjust the schedule.
            </p>
          }

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
export class AutocallablePanelComponent extends PricingPanelBase implements OnInit {
  readonly kind: InstrumentKind = 'autocallable';

  //! autocallable prices on pde / mcl only (no ANA closed form)
  override engine: Engine = 'mcl';
  flavour: 'athena' | 'phoenix' = 'athena';
  couponMemory = false;

  nominal = 100;
  coupon = 6;
  autocallBarrier = 100;
  protectionBarrier = 60;
  couponBarrier = 70;

  firstObservation: Date | null = null;
  frequencyMonths = 6;
  observations = 4;

  ngOnInit(): void {
    this.init();
    // default the first observation ~one frequency out from today (the workspace
    // valuation date), so a schedule is ready as soon as an underlying is picked.
    if (!this.firstObservation) {
      const ws = this.ctx.workspace();
      const base = ws ? new Date(`${ws.today}T00:00:00`) : new Date();
      this.firstObservation = new Date(
        base.getFullYear(),
        base.getMonth() + this.frequencyMonths,
        base.getDate(),
      );
    }
  }

  //! the generated autocall schedule (ISO), every date strictly before maturity.
  scheduleDates(): string[] {
    const first = this.firstObservation;
    const mat = this.maturityIso;
    if (!first || mat == null || this.observations < 1 || this.frequencyMonths < 1) return [];
    const dates: string[] = [];
    for (let k = 0; k < this.observations; k++) {
      const d = new Date(first.getFullYear(), first.getMonth() + k * this.frequencyMonths, first.getDate());
      const iso = `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
      if (iso < mat) dates.push(iso); //!< strictly before maturity (the redemption obs)
    }
    return dates;
  }

  protected buildFields(): Record<string, unknown> | null {
    const maturity = this.maturityIso;
    const autocall_dates = this.scheduleDates();
    if (!this.underlying || maturity == null || !autocall_dates.length) return null;
    if (!Number.isFinite(this.autocallBarrier) || !Number.isFinite(this.protectionBarrier)) return null;
    if (this.protectionBarrier > this.autocallBarrier) return null; //!< engine rejects it too

    const fields: Record<string, unknown> = {
      underlying: this.underlying,
      maturity,
      autocall_dates,
      autocall_barrier: this.autocallBarrier,
      protection_barrier: this.protectionBarrier,
      coupon: this.coupon,
      nominal: this.nominal,
    };
    if (this.flavour === 'phoenix') {
      fields['coupon_barrier'] = this.couponBarrier;
      if (this.couponMemory) fields['coupon_memory'] = true;
    }
    return fields;
  }

  rowLabel(): string {
    const flav = this.flavour === 'phoenix' ? (this.couponMemory ? 'Phoenix-mem' : 'Phoenix') : 'Athena';
    return `${this.underlying} ${flav} ${this.coupon}% ${this.maturityIso}`;
  }
}

function pad(n: number): string {
  return String(n).padStart(2, '0');
}
