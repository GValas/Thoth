import { Component, Input } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { PricingPanelBase } from './pricing-panel.base';

//! Shared action bar for the pricing panels: the live throttle, the Live toggle, the one-off
//! Price button and the "Send to blotter" button, plus the status / provenance lines. Bound
//! to the panel instance so all three panels share identical pricing controls and charte.
@Component({
  selector: 'app-panel-actions',
  standalone: true,
  imports: [
    FormsModule,
    MatButtonModule,
    MatFormFieldModule,
    MatInputModule,
    MatIconModule,
    MatTooltipModule,
  ],
  template: `
    <div class="actions-row">
      <mat-form-field appearance="outline" subscriptSizing="dynamic" class="throttle">
        <mat-label>Live every (s)</mat-label>
        <input matInput type="number" min="1" [(ngModel)]="panel.liveThrottleSec" />
      </mat-form-field>
      <button
        mat-stroked-button
        [color]="panel.liveMode() ? 'primary' : undefined"
        [disabled]="!panel.canPrice"
        (click)="panel.toggleLive()"
        [matTooltip]="
          panel.liveMode()
            ? 'Live re-pricing on — click to stop'
            : 'Re-price continuously off the live spots'
        "
      >
        <mat-icon>{{ panel.liveMode() ? 'sensors' : 'sensors_off' }}</mat-icon> Live
      </button>
      <button mat-flat-button color="primary" [disabled]="!panel.canPrice || panel.busy()" (click)="panel.price()">
        Price
      </button>
      <span class="thoth-spacer"></span>
      <button
        mat-stroked-button
        [disabled]="!panel.canPrice"
        (click)="panel.addToBlotter()"
        matTooltip="Send this product to the global monitoring blotter"
      >
        <mat-icon>playlist_add</mat-icon> Send to blotter
      </button>
    </div>

    @if (panel.error()) {
      <p class="thoth-error">{{ panel.error() }}</p>
    }
    @if (panel.liveMode()) {
      <p class="meta thoth-muted">
        <span class="dot-live"></span> Live — re-pricing every {{ panel.liveThrottleSec }}s off the live spots
      </p>
    }
    @if (panel.meta(); as meta) {
      <p class="meta thoth-muted">
        <mat-icon>schedule</mat-icon>
        @if (meta.server) { Priced on <code>{{ meta.server }}</code> · }
        @if (meta.engineMs != null) { engine {{ meta.engineMs }} ms }
        @if (meta.execMs != null) { · round-trip {{ meta.execMs }} ms }
        @if (meta.engineVersion) { · {{ meta.engineVersion }} }
      </p>
    }
  `,
  styleUrl: './panel.shared.scss',
})
export class PanelActionsComponent {
  @Input({ required: true }) panel!: PricingPanelBase;
}
